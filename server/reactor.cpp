#include "reactor.h"

#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/event.h>
#include <sys/time.h>
#else
#error "Reactor backend requires epoll on Linux or kqueue on BSD/macOS."
#endif

#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void throwErrno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

#if defined(__linux__)
std::uint32_t toNative(Reactor::Interest interest) {
    return interest == Reactor::Interest::Read ? EPOLLIN : EPOLLOUT;
}
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
std::uint32_t toNative(Reactor::Interest interest) {
    return interest == Reactor::Interest::Read ? EVFILT_READ : EVFILT_WRITE;
}
#endif

} // namespace

bool Reactor::EventAwaiter::await_ready() const noexcept {
    return false;
}

void Reactor::EventAwaiter::await_suspend(std::coroutine_handle<> handle) {
    reactor.addOrUpdate(fd, interest, handle);
}

Reactor::Reactor() {
#if defined(__linux__)
    backendFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (backendFd_ < 0) {
        throwErrno("epoll_create1");
    }
#else
    backendFd_ = kqueue();
    if (backendFd_ < 0) {
        throwErrno("kqueue");
    }
#endif
}

Reactor::~Reactor() {
    if (backendFd_ >= 0) {
        close(backendFd_);
    }
}

Reactor::EventAwaiter Reactor::readable(int fd) noexcept {
    return EventAwaiter{*this, fd, Interest::Read};
}

Reactor::EventAwaiter Reactor::writable(int fd) noexcept {
    return EventAwaiter{*this, fd, Interest::Write};
}

void Reactor::addOrUpdate(int fd, Interest interest, std::coroutine_handle<> handle) {
    if (interest == Interest::Read) {
        readWaiters_[fd] = handle;
    } else {
        writeWaiters_[fd] = handle;
    }

#if defined(__linux__)
    const std::uint32_t wanted = toNative(interest);
    std::uint32_t events = EPOLLET | EPOLLRDHUP | wanted;
    if (readWaiters_.contains(fd)) {
        events |= EPOLLIN;
    }
    if (writeWaiters_.contains(fd)) {
        events |= EPOLLOUT;
    }

    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(backendFd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        if (errno == ENOENT) {
            if (epoll_ctl(backendFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
                throwErrno("epoll_ctl ADD");
            }
            return;
        }
        throwErrno("epoll_ctl MOD");
    }
#else
    struct kevent event{};
    EV_SET(&event, static_cast<uintptr_t>(fd), static_cast<int16_t>(toNative(interest)),
           EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
    if (kevent(backendFd_, &event, 1, nullptr, 0, nullptr) < 0) {
        throwErrno("kevent register");
    }
#endif
}

void Reactor::removeWaiter(int fd, std::uint32_t events) noexcept {
#if defined(__linux__)
    if ((events & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
        readWaiters_.erase(fd);
    }
    if ((events & (EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
        writeWaiters_.erase(fd);
    }

    if (!readWaiters_.contains(fd) && !writeWaiters_.contains(fd)) {
        epoll_ctl(backendFd_, EPOLL_CTL_DEL, fd, nullptr);
    }
#else
    if ((events & EVFILT_READ) != 0) {
        readWaiters_.erase(fd);
    }
    if ((events & EVFILT_WRITE) != 0) {
        writeWaiters_.erase(fd);
    }
#endif
}

void Reactor::run() {
    constexpr int maxEvents = 128;

#if defined(__linux__)
    std::vector<epoll_event> events(maxEvents);
#else
    std::vector<struct kevent> events(maxEvents);
#endif

    while (running_) {
#if defined(__linux__)
        const int count = epoll_wait(backendFd_, events.data(), maxEvents, -1);
#else
        const int count = kevent(backendFd_, nullptr, 0, events.data(), maxEvents, nullptr);
#endif
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
#if defined(__linux__)
            throwErrno("epoll_wait");
#else
            throwErrno("kevent wait");
#endif
        }

        for (int i = 0; i < count; ++i) {
#if defined(__linux__)
            const int fd = events[i].data.fd;
            const std::uint32_t flags = events[i].events;
            const bool readReady = (flags & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
            const bool writeReady = (flags & (EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
#else
            const int fd = static_cast<int>(events[i].ident);
            const std::uint32_t flags = static_cast<std::uint32_t>(events[i].filter);
            const bool readReady = events[i].filter == EVFILT_READ || (events[i].flags & EV_EOF) != 0;
            const bool writeReady = events[i].filter == EVFILT_WRITE || (events[i].flags & EV_EOF) != 0;
#endif

            std::coroutine_handle<> readHandle;
            std::coroutine_handle<> writeHandle;

            if (readReady) {
                if (auto it = readWaiters_.find(fd); it != readWaiters_.end()) {
                    readHandle = it->second;
                }
            }
            if (writeReady) {
                if (auto it = writeWaiters_.find(fd); it != writeWaiters_.end()) {
                    writeHandle = it->second;
                }
            }

            removeWaiter(fd, flags);

            if (readHandle) {
                readHandle.resume();
            }
            if (writeHandle && writeHandle != readHandle) {
                writeHandle.resume();
            }
        }
    }
}

void Reactor::stop() noexcept {
    running_ = false;
}

int Reactor::backendFd() const noexcept {
    return backendFd_;
}
