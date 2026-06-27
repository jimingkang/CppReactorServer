#include "game2048.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace {

struct Session {
    int id = 0;
    std::string input;
    std::string output;
    bool closing = false;
};

int setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int createListenSocket(const std::string& host, int port) {
    const int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        throw std::runtime_error(std::string("socket: ") + std::strerror(errno));
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#if defined(SO_NOSIGPIPE)
    setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close(fd);
        throw std::runtime_error("invalid host: " + host);
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(fd);
        throw std::runtime_error(std::string("bind: ") + std::strerror(errno));
    }
    if (listen(fd, SOMAXCONN) != 0) {
        close(fd);
        throw std::runtime_error(std::string("listen: ") + std::strerror(errno));
    }
    setNonBlocking(fd);
    return fd;
}

std::string trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

void appendState(Game2048& game, Session& session) {
    session.output += game.handleCommand(session.id, "STATE");
}

void handleCommand(Game2048& game, Session& session, const std::string& line) {
    std::string response = game.handleCommand(session.id, line);
    if (response == "QUIT\n") {
        session.output += "BYE player=" + std::to_string(session.id) + "\n";
        session.closing = true;
        return;
    }

    session.output += response;
    if (line == "HELP" || line == "help") {
        return;
    }
    appendState(game, session);
}

void acceptClient(int listenFd, std::unordered_map<int, Session>& sessions, Game2048& game) {
    while (true) {
        sockaddr_in addr{};
        socklen_t len = sizeof(addr);
        const int fd = accept(listenFd, reinterpret_cast<sockaddr*>(&addr), &len);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            throw std::runtime_error(std::string("accept: ") + std::strerror(errno));
        }

        setNonBlocking(fd);
#if defined(SO_NOSIGPIPE)
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
        Session session;
        session.id = game.join();
        session.output = "WELCOME player=" + std::to_string(session.id) + " game=2048\n";
        session.output += "COMMANDS STATE | MOVE LEFT|RIGHT|UP|DOWN | RESET | QUIT\n";
        appendState(game, session);
        sessions.emplace(fd, std::move(session));
    }
}

bool readClient(int fd, Session& session, Game2048& game) {
    char buffer[2048];
    while (true) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            session.input.append(buffer, static_cast<std::size_t>(n));
            std::size_t pos = 0;
            while ((pos = session.input.find('\n')) != std::string::npos) {
                std::string line = trimLine(session.input.substr(0, pos + 1));
                session.input.erase(0, pos + 1);
                if (!line.empty()) {
                    handleCommand(game, session, line);
                }
            }
            continue;
        }
        if (n == 0) {
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return true;
        }
        return false;
    }
}

bool writeClient(int fd, Session& session) {
    while (!session.output.empty()) {
#if defined(MSG_NOSIGNAL)
        constexpr int flags = MSG_NOSIGNAL;
#else
        constexpr int flags = 0;
#endif
        const ssize_t n = send(fd, session.output.data(), session.output.size(), flags);
        if (n > 0) {
            session.output.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return true;
        }
        return false;
    }
    return !session.closing;
}

} // namespace

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const int port = argc > 2 ? std::atoi(argv[2]) : 7788;

    try {
        const int listenFd = createListenSocket(host, port);
        std::unordered_map<int, Session> sessions;
        Game2048 game;

        std::cout << "game2048_server listening on " << host << ':' << port << '\n';
        while (true) {
            fd_set readSet;
            fd_set writeSet;
            FD_ZERO(&readSet);
            FD_ZERO(&writeSet);
            FD_SET(listenFd, &readSet);
            int maxFd = listenFd;

            for (const auto& [fd, session] : sessions) {
                FD_SET(fd, &readSet);
                if (!session.output.empty()) {
                    FD_SET(fd, &writeSet);
                }
                maxFd = std::max(maxFd, fd);
            }

            const int ready = select(maxFd + 1, &readSet, &writeSet, nullptr, nullptr);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::runtime_error(std::string("select: ") + std::strerror(errno));
            }

            if (FD_ISSET(listenFd, &readSet)) {
                acceptClient(listenFd, sessions, game);
            }

            for (auto it = sessions.begin(); it != sessions.end();) {
                const int fd = it->first;
                bool keep = true;
                if (FD_ISSET(fd, &readSet)) {
                    keep = readClient(fd, it->second, game);
                }
                if (keep && FD_ISSET(fd, &writeSet)) {
                    keep = writeClient(fd, it->second);
                }

                if (!keep) {
                    game.leave(it->second.id);
                    close(fd);
                    it = sessions.erase(it);
                } else {
                    ++it;
                }
            }
        }
    } catch (const std::exception& ex) {
        std::cerr << "game2048_server error: " << ex.what() << '\n';
        return 1;
    }
}
