#include "coro_server.h"

#include "socket_utils.h"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace {

#if defined(MSG_NOSIGNAL)
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;
#endif

std::string trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

std::string ensureNewline(std::string data) {
    if (data.empty() || data.back() != '\n') {
        data.push_back('\n');
    }
    return data;
}

int acceptNonBlocking(int listenFd) {
    sockaddr_in clientAddr{};
    socklen_t len = sizeof(clientAddr);
#if defined(__linux__)
    return accept4(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
    const int clientFd = accept(listenFd, reinterpret_cast<sockaddr*>(&clientAddr), &len);
    if (clientFd >= 0) {
        setNonBlocking(clientFd);
        int yes = 1;
#if defined(SO_NOSIGPIPE)
        setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
    }
    return clientFd;
#endif
}

} // namespace

CoroGameServer::CoroGameServer(CoroReactor& reactor, std::string host, int port)
    : reactor_(reactor), host_(std::move(host)), port_(port) {}

CoroGameServer::~CoroGameServer() {
    if (listenFd_ >= 0) {
        close(listenFd_);
    }
}

void CoroGameServer::start() {
    listenFd_ = createListenSocket(host_, port_);
    std::cout << "game_server_coro listening on " << host_ << ':' << port_ << '\n';
    acceptLoop();
}

DetachedTask CoroGameServer::acceptLoop() {
    while (true) {
        const int clientFd = acceptNonBlocking(listenFd_);
        if (clientFd >= 0) {
            clientLoop(clientFd);
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            co_await reactor_.waitReadable(listenFd_);
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        throw std::runtime_error(std::string("accept: ") + std::strerror(errno));
    }
}

DetachedTask CoroGameServer::clientLoop(int clientFd) {
    const int playerId = world_.join();
    std::string output = ensureNewline("WELCOME player=" + std::to_string(playerId) + " type HELP");
    size_t outputOffset = 0;
    std::string inputBuffer;
    char chunk[2048];

    while (true) {
        while (outputOffset < output.size()) {
            const ssize_t n = send(clientFd, output.data() + outputOffset, output.size() - outputOffset, kSendFlags);
            if (n > 0) {
                outputOffset += static_cast<size_t>(n);
                continue;
            }
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                co_await reactor_.waitWritable(clientFd);
                continue;
            }
            if (n < 0 && errno == EINTR) {
                continue;
            }
            world_.leave(playerId);
            close(clientFd);
            co_return;
        }
        output.clear();
        outputOffset = 0;

        const ssize_t n = recv(clientFd, chunk, sizeof(chunk), 0);
        if (n > 0) {
            inputBuffer.append(chunk, static_cast<size_t>(n));
            size_t newline = 0;
            while ((newline = inputBuffer.find('\n')) != std::string::npos) {
                std::string line = trimLine(inputBuffer.substr(0, newline + 1));
                inputBuffer.erase(0, newline + 1);
                if (line.empty()) {
                    continue;
                }

                const std::string response = world_.handleCommand(playerId, line);
                if (response == "QUIT\n") {
                    output += ensureNewline(world_.leave(playerId));
                    while (outputOffset < output.size()) {
                        const ssize_t written = send(clientFd, output.data() + outputOffset,
                                                     output.size() - outputOffset, kSendFlags);
                        if (written > 0) {
                            outputOffset += static_cast<size_t>(written);
                            continue;
                        }
                        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                            co_await reactor_.waitWritable(clientFd);
                            continue;
                        }
                        break;
                    }
                    close(clientFd);
                    co_return;
                }
                output += response;
            }
            continue;
        }

        if (n == 0) {
            world_.leave(playerId);
            close(clientFd);
            co_return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            co_await reactor_.waitReadable(clientFd);
            continue;
        }
        if (errno == EINTR) {
            continue;
        }

        world_.leave(playerId);
        close(clientFd);
        co_return;
    }
}

DetachedTask CoroGameServer::writeAll(int fd, std::string data) {
    size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t n = send(fd, data.data() + offset, data.size() - offset, kSendFlags);
        if (n > 0) {
            offset += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            co_await reactor_.waitWritable(fd);
            continue;
        }
        co_return;
    }
}
