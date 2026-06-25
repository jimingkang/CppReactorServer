#include "coro_reactor.h"

#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/event.h>
#include <sys/time.h>
#else
#error "CoroReactor requires Linux epoll or macOS/BSD kqueue."
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
std::uint32_t nativeEvent(CoroReactor::EventKind kind) {
    return kind == CoroReactor::EventKind::Read ? EPOLLIN : EPOLLOUT;
}
#else
std::int16_t nativeEvent(CoroReactor::EventKind kind) {
    return kind == CoroReactor::EventKind::Read ? EVFILT_READ : EVFILT_WRITE;
}
#endif

} // namespace

CoroReactor::IoAwaiter::IoAwaiter(CoroReactor& reactor, int fd, EventKind kind) noexcept
    : reactor_(reactor), fd_(fd), kind_(kind) {}

bool CoroReactor::IoAwaiter::await_ready() const noexcept {
    return false;
}

void CoroReactor::IoAwaiter::await_suspend(std::coroutine_handle<> coroutine) {
    reactor_.arm(fd_, kind_, coroutine);
}

void CoroReactor::IoAwaiter::await_resume() const noexcept {}

CoroReactor::CoroReactor() {
#if defined(__linux__)
    eventFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (eventFd_ < 0) {
        throwErrno("epoll_create1");
    }
#else
    eventFd_ = kqueue();
    if (eventFd_ < 0) {
        throwErrno("kqueue");
    }
#endif
}

CoroReactor::~CoroReactor() {
    if (eventFd_ >= 0) {
        close(eventFd_);
    }
}

CoroReactor::IoAwaiter CoroReactor::waitReadable(int fd) noexcept {
    return IoAwaiter{*this, fd, EventKind::Read};
}

CoroReactor::IoAwaiter CoroReactor::waitWritable(int fd) noexcept {
    return IoAwaiter{*this, fd, EventKind::Write};
}

void CoroReactor::arm(int fd, EventKind kind, std::coroutine_handle<> coroutine) {
    if (kind == EventKind::Read) {
        readCoroutines_[fd] = coroutine;
    } else {
        writeCoroutines_[fd] = coroutine;
    }

#if defined(__linux__)
    std::uint32_t events = EPOLLET | EPOLLRDHUP | nativeEvent(kind);
    if (readCoroutines_.contains(fd)) {
        events |= EPOLLIN;
    }
    if (writeCoroutines_.contains(fd)) {
        events |= EPOLLOUT;
    }

    epoll_event event{};
    event.events = events;
    event.data.fd = fd;

    if (epoll_ctl(eventFd_, EPOLL_CTL_MOD, fd, &event) < 0) {
        if (errno == ENOENT) {
            if (epoll_ctl(eventFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
                throwErrno("epoll_ctl ADD");
            }
            return;
        }
        throwErrno("epoll_ctl MOD");
    }
#else
    struct kevent change{};
    EV_SET(&change, static_cast<uintptr_t>(fd), nativeEvent(kind), EV_ADD | EV_ENABLE | EV_ONESHOT, 0, 0, nullptr);
    if (kevent(eventFd_, &change, 1, nullptr, 0, nullptr) < 0) {
        throwErrno("kevent register");
    }
#endif
}

void CoroReactor::disarmDelivered(int fd, bool readable, bool writable) noexcept {
    if (readable) {
        readCoroutines_.erase(fd);
    }
    if (writable) {
        writeCoroutines_.erase(fd);
    }

#if defined(__linux__)
    if (!readCoroutines_.contains(fd) && !writeCoroutines_.contains(fd)) {
        epoll_ctl(eventFd_, EPOLL_CTL_DEL, fd, nullptr);
    }
#endif
}

void CoroReactor::run() {
    constexpr int maxEvents = 128;
#if defined(__linux__)
    std::vector<epoll_event> events(maxEvents);
#else
    std::vector<struct kevent> events(maxEvents);
#endif

    while (running_) {
#if defined(__linux__)
        const int count = epoll_wait(eventFd_, events.data(), maxEvents, -1);
#else
        const int count = kevent(eventFd_, nullptr, 0, events.data(), maxEvents, nullptr);
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
            const auto flags = events[i].events;
            const bool readable = (flags & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
            const bool writable = (flags & (EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
#else
            const int fd = static_cast<int>(events[i].ident);
            const bool eof = (events[i].flags & EV_EOF) != 0;
            const bool readable = events[i].filter == EVFILT_READ || eof;
            const bool writable = events[i].filter == EVFILT_WRITE || eof;
#endif

            std::coroutine_handle<> reader;
            std::coroutine_handle<> writer;

            if (readable) {
                if (auto it = readCoroutines_.find(fd); it != readCoroutines_.end()) {
                    reader = it->second;
                }
            }
            if (writable) {
                if (auto it = writeCoroutines_.find(fd); it != writeCoroutines_.end()) {
                    writer = it->second;
                }
            }

            disarmDelivered(fd, readable, writable);

            if (reader) {
                reader.resume();
            }
            if (writer && writer != reader) {
                writer.resume();
            }
        }
    }
}

void CoroReactor::stop() noexcept {
    running_ = false;
}
