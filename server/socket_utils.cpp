#include "socket_utils.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

void throwErrno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

} // namespace

int setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throwErrno("fcntl F_GETFL");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throwErrno("fcntl F_SETFL");
    }
    return fd;
}

int createListenSocket(const std::string& host, int port, int backlog) {
#if defined(__linux__)
    const int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
#else
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    if (fd < 0) {
        throwErrno("socket");
    }
    setNonBlocking(fd);

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#if defined(SO_REUSEPORT)
    setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(yes));
#endif
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("invalid listen host: " + host);
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        throwErrno("bind");
    }
    if (listen(fd, backlog) < 0) {
        close(fd);
        throwErrno("listen");
    }
    return fd;
}
