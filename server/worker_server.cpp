#include "worker_server.h"

#include "socket_utils.h"

#if defined(__linux__)
#include <sys/epoll.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#include <sys/event.h>
#include <sys/time.h>
#else
#error "WorkerGameServer requires epoll on Linux or kqueue on BSD/macOS."
#endif

#include <arpa/inet.h>
#include <fcntl.h>
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

void throwErrno(const char* what) {
    throw std::runtime_error(std::string(what) + ": " + std::strerror(errno));
}

void setFdNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throwErrno("fcntl nonblocking");
    }
}

std::size_t serviceIndex(ServiceId id) {
    return static_cast<std::size_t>(id) - 1;
}

} // namespace

ServiceQueue::ServiceQueue(ServiceId id) : id_(id) {}

ServiceId ServiceQueue::id() const noexcept {
    return id_;
}

bool ServiceQueue::push(SkynetMessage message) {
    std::lock_guard lock(mutex_);
    queue_.push(std::move(message));
    if (scheduled_) {
        return false;
    }
    scheduled_ = true;
    return true;
}

bool ServiceQueue::popOne(SkynetMessage& message) {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        return false;
    }
    message = std::move(queue_.front());
    queue_.pop();
    return true;
}

bool ServiceQueue::finishBatch() {
    std::lock_guard lock(mutex_);
    if (queue_.empty()) {
        scheduled_ = false;
        return false;
    }
    return true;
}

WorkerGameServer::WorkerGameServer(std::string host, int port, std::size_t workerCount)
    : host_(std::move(host)), port_(port), workerCount_(workerCount == 0 ? 1 : workerCount) {
    services_.reserve(6);
    services_.push_back(std::make_unique<ServiceQueue>(ServiceId::Logger));
    services_.push_back(std::make_unique<ServiceQueue>(ServiceId::Gate));
    services_.push_back(std::make_unique<ServiceQueue>(ServiceId::Connection));
    services_.push_back(std::make_unique<ServiceQueue>(ServiceId::GameWorld));
    services_.push_back(std::make_unique<ServiceQueue>(ServiceId::Room));
    services_.push_back(std::make_unique<ServiceQueue>(ServiceId::Db));
}

WorkerGameServer::~WorkerGameServer() {
    stop();
}

void WorkerGameServer::run() {
    listenFd_ = createListenSocket(host_, port_);

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

    int wakePipe[2]{};
    if (pipe(wakePipe) < 0) {
        throwErrno("pipe");
    }
    wakeReadFd_ = wakePipe[0];
    wakeWriteFd_ = wakePipe[1];
    setFdNonBlocking(wakeReadFd_);
    setFdNonBlocking(wakeWriteFd_);

    running_.store(true);
    addReadFd(wakeReadFd_);
    addReadFd(listenFd_);

    workers_.reserve(workerCount_);
    for (std::size_t i = 0; i < workerCount_; ++i) {
        workers_.emplace_back([this, i] { workerLoop(i); });
    }

    // Start fixed-step tick thread for authoritative simulation (100ms per tick).
    tickThread_ = std::thread([this] {
        using namespace std::chrono_literals;
        const std::chrono::milliseconds interval(100);
        while (running_.load()) {
            const auto start = std::chrono::steady_clock::now();
            try {
                world_.tick(static_cast<int>(interval.count()));
            } catch (const std::exception& ex) {
                std::cerr << "world.tick error: " << ex.what() << '\n';
            }
            const auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed < interval) {
                std::this_thread::sleep_for(interval - elapsed);
            }
        }
    });

    std::cout << "worker_game_server listening on " << host_ << ':' << port_
              << " workers=" << workerCount_ << " services=logger,gate,connection,gameworld,room,db\n";
    socketLoop();
}

void WorkerGameServer::stop() {
    const bool wasRunning = running_.exchange(false);
    if (wasRunning) {
        wakeSocketLoop();
    }

    {
        std::lock_guard lock(globalMutex_);
        servicesStopped_ = true;
    }
    globalReady_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();

    if (tickThread_.joinable()) {
        tickThread_.join();
    }

    for (auto& [fd, _] : sockets_) {
        close(fd);
    }
    sockets_.clear();

    if (listenFd_ >= 0) {
        close(listenFd_);
        listenFd_ = -1;
    }
    if (wakeReadFd_ >= 0) {
        close(wakeReadFd_);
        wakeReadFd_ = -1;
    }
    if (wakeWriteFd_ >= 0) {
        close(wakeWriteFd_);
        wakeWriteFd_ = -1;
    }
    if (backendFd_ >= 0) {
        close(backendFd_);
        backendFd_ = -1;
    }
}

void WorkerGameServer::socketLoop() {
    constexpr int maxEvents = 128;
#if defined(__linux__)
    std::vector<epoll_event> events(maxEvents);
#else
    std::vector<struct kevent> events(maxEvents);
#endif

    while (running_.load()) {
#if defined(__linux__)
        const int count = epoll_wait(backendFd_, events.data(), maxEvents, -1);
#else
        const int count = kevent(backendFd_, nullptr, 0, events.data(), maxEvents, nullptr);
#endif
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throwErrno("socket event wait");
        }

        for (int i = 0; i < count; ++i) {
#if defined(__linux__)
            const int fd = events[i].data.fd;
            const auto flags = events[i].events;
            const bool readable = (flags & (EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
            const bool writable = (flags & EPOLLOUT) != 0;
#else
            const int fd = static_cast<int>(events[i].ident);
            const bool readable = events[i].filter == EVFILT_READ || (events[i].flags & EV_EOF) != 0;
            const bool writable = events[i].filter == EVFILT_WRITE;
#endif

            if (fd == wakeReadFd_) {
                char buffer[64];
                while (read(wakeReadFd_, buffer, sizeof(buffer)) > 0) {}
                drainSocketCommands();
                continue;
            }

            if (fd == listenFd_) {
                acceptClients();
                continue;
            }

            if (readable) {
                readClient(fd);
            }
            if (writable && sockets_.contains(fd)) {
                flushClient(fd);
            }
        }

        drainSocketCommands();
    }
}

void WorkerGameServer::workerLoop(std::size_t workerId) {
    (void)workerId;
    while (true) {
        ServiceQueue* queue = waitReadyService();
        if (queue == nullptr) {
            return;
        }

        SkynetMessage message;
        int handled = 0;
        constexpr int maxBatch = 64;
        while (handled < maxBatch && queue->popOne(message)) {
            dispatchMessage(message);
            ++handled;
        }

        if (queue->finishBatch()) {
            rescheduleService(*queue);
        }
    }
}

void WorkerGameServer::sendToService(ServiceId destination, SkynetMessage message) {
    message.destination = destination;
    ServiceQueue& queue = serviceQueue(destination);
    if (!queue.push(std::move(message))) {
        return;
    }
    rescheduleService(queue);
}

ServiceQueue* WorkerGameServer::waitReadyService() {
    std::unique_lock lock(globalMutex_);
    globalReady_.wait(lock, [&] { return servicesStopped_ || !globalQueue_.empty(); });
    if (globalQueue_.empty()) {
        return nullptr;
    }
    ServiceQueue* queue = globalQueue_.front();
    globalQueue_.pop();
    return queue;
}

void WorkerGameServer::rescheduleService(ServiceQueue& queue) {
    {
        std::lock_guard lock(globalMutex_);
        if (servicesStopped_) {
            return;
        }
        globalQueue_.push(&queue);
    }
    globalReady_.notify_one();
}

ServiceQueue& WorkerGameServer::serviceQueue(ServiceId id) {
    return *services_.at(serviceIndex(id));
}

void WorkerGameServer::dispatchMessage(const SkynetMessage& message) {
    switch (message.destination) {
    case ServiceId::Logger:
        handleLoggerService(message);
        break;
    case ServiceId::Gate:
        handleGateService(message);
        break;
    case ServiceId::Connection:
        handleConnectionService(message);
        break;
    case ServiceId::GameWorld:
        handleGameWorldService(message);
        break;
    case ServiceId::Room:
        handleRoomService(message);
        break;
    case ServiceId::Db:
        handleDbService(message);
        break;
    }
}

void WorkerGameServer::handleLoggerService(const SkynetMessage& message) {
    if (message.kind == MessageKind::Log && !message.text.empty()) {
        std::cout << message.text << '\n';
    }
}

void WorkerGameServer::handleGateService(const SkynetMessage& message) {
    if (message.kind != MessageKind::Socket) {
        return;
    }

    SkynetMessage forwarded;
    forwarded.source = ServiceId::Gate;
    forwarded.kind = MessageKind::Socket;
    forwarded.socket = message.socket;
    sendToService(ServiceId::Connection, std::move(forwarded));
}

void WorkerGameServer::handleConnectionService(const SkynetMessage& message) {
    if (message.kind == MessageKind::Socket) {
        const SocketMessage& socket = message.socket;
        switch (socket.type) {
        case SocketMessageType::Accept: {
            sessions_.emplace(socket.fd, supermario::SuperMarioSessionAgent{socket.fd});
            SkynetMessage command;
            command.source = ServiceId::Connection;
            command.kind = MessageKind::GameCommand;
            command.gameCommand = sessions_.at(socket.fd).makeJoinCommand();
            sendToService(ServiceId::GameWorld, std::move(command));
            break;
        }
        case SocketMessageType::Data: {
            auto it = sessions_.find(socket.fd);
            if (it == sessions_.end() || it->second.closing()) {
                break;
            }

            auto commands = it->second.onSocketData(socket.data);
            for (auto& gameCommand : commands) {
                SkynetMessage command;
                command.source = ServiceId::Connection;
                command.kind = MessageKind::GameCommand;
                command.gameCommand = std::move(gameCommand);
                sendToService(ServiceId::GameWorld, std::move(command));
            }
            break;
        }
        case SocketMessageType::Close:
        case SocketMessageType::Error: {
            auto it = sessions_.find(socket.fd);
            if (it == sessions_.end()) {
                queueSocketCommand(SocketCommand{SocketCommandType::Close, socket.fd, {}});
                break;
            }
            const auto plan = it->second.beginDisconnect();
            if (plan.sendLeaveToWorld) {
                SkynetMessage command;
                command.source = ServiceId::Connection;
                command.kind = MessageKind::GameCommand;
                command.gameCommand = it->second.makeLeaveCommand();
                sendToService(ServiceId::GameWorld, std::move(command));
            }
            if (plan.eraseSession) {
                sessions_.erase(it);
            }
            if (plan.closeSocket) {
                queueSocketCommand(SocketCommand{SocketCommandType::Close, socket.fd, {}});
            }
            break;
        }
        }
        return;
    }

    if (message.kind != MessageKind::GameResponse) {
        return;
    }

    const GameResponse& response = message.gameResponse;
    auto it = sessions_.find(response.fd);
    if (it == sessions_.end()) {
        return;
    }

    auto plan = it->second.onWorldResponse(response);
    if (plan.eraseSession) {
        sessions_.erase(response.fd);
    }
    if (!plan.outboundText.empty()) {
        queueSocketCommand(SocketCommand{SocketCommandType::Send, response.fd, std::move(plan.outboundText)});
    }
    if (plan.closeSocket) {
        queueSocketCommand(SocketCommand{SocketCommandType::Close, response.fd, {}});
    }
}

void WorkerGameServer::handleGameWorldService(const SkynetMessage& message) {
    if (message.kind != MessageKind::GameCommand) {
        return;
    }

    const GameCommand& command = message.gameCommand;
    GameResponse response;
    response.fd = command.fd;

    if (command.type == GameCommandType::Join) {
        const int playerId = world_.join();
        response.type = GameResponseType::Joined;
        response.playerId = playerId;
        response.text += world_.snapshot();

        SkynetMessage room;
        room.source = ServiceId::GameWorld;
        room.kind = MessageKind::Room;
        room.room = RoomMessage{RoomMessageType::PlayerJoined, playerId};
        sendToService(ServiceId::Room, std::move(room));
    } else if (command.type == GameCommandType::Leave) {
        response.type = GameResponseType::LeaveAck;
        response.playerId = command.playerId;
        response.text = world_.leave(command.playerId);
        response.closeAfterSend = true;

        SkynetMessage room;
        room.source = ServiceId::GameWorld;
        room.kind = MessageKind::Room;
        room.room = RoomMessage{RoomMessageType::PlayerLeft, command.playerId};
        sendToService(ServiceId::Room, std::move(room));

        SkynetMessage db;
        db.source = ServiceId::GameWorld;
        db.kind = MessageKind::Db;
        db.db = DbMessage{DbMessageType::SavePlayer, command.playerId};
        sendToService(ServiceId::Db, std::move(db));
    } else {
        std::string result = world_.handleCommand(command.playerId, command.line);
        if (result == "QUIT\n") {
            response.type = GameResponseType::LeaveAck;
            response.playerId = command.playerId;
            response.text = world_.leave(command.playerId);
            response.closeAfterSend = true;
        } else {
            response.type = GameResponseType::Text;
            response.playerId = command.playerId;
            response.text = std::move(result);
        }
    }

    SkynetMessage outbound;
    outbound.source = ServiceId::GameWorld;
    outbound.kind = MessageKind::GameResponse;
    outbound.gameResponse = std::move(response);
    sendToService(ServiceId::Connection, std::move(outbound));
}

void WorkerGameServer::handleRoomService(const SkynetMessage& message) {
    if (message.kind != MessageKind::Room) {
        return;
    }
    // RoomService is a placeholder for room/map sharding. It intentionally has
    // its own queue so workers can schedule it independently from GameWorld.
}

void WorkerGameServer::handleDbService(const SkynetMessage& message) {
    if (message.kind != MessageKind::Db) {
        return;
    }
    // DbService is a placeholder for async persistence. Real implementations
    // would batch writes or hand them to a database client thread here.
}

void WorkerGameServer::queueSocketMessage(SocketMessage message) {
    SkynetMessage wrapped;
    wrapped.source = ServiceId::Gate;
    wrapped.kind = MessageKind::Socket;
    wrapped.socket = std::move(message);
    sendToService(ServiceId::Gate, std::move(wrapped));
}

void WorkerGameServer::queueSocketCommand(SocketCommand command) {
    {
        std::lock_guard lock(commandMutex_);
        socketCommands_.push(std::move(command));
    }
    wakeSocketLoop();
}

void WorkerGameServer::drainSocketCommands() {
    std::queue<SocketCommand> commands;
    {
        std::lock_guard lock(commandMutex_);
        commands.swap(socketCommands_);
    }

    while (!commands.empty()) {
        SocketCommand command = std::move(commands.front());
        commands.pop();

        auto it = sockets_.find(command.fd);
        if (it == sockets_.end()) {
            continue;
        }

        if (command.type == SocketCommandType::Send) {
            it->second.output += command.data;
            flushClient(command.fd);
        } else {
            it->second.closing = true;
            if (it->second.output.empty()) {
                closeClientNow(command.fd);
            } else {
                updateFdInterest(command.fd, true, true);
            }
        }
    }
}

void WorkerGameServer::wakeSocketLoop() {
    if (wakeWriteFd_ < 0) {
        return;
    }
    const char byte = 1;
    const ssize_t n = write(wakeWriteFd_, &byte, 1);
    (void)n;
}

void WorkerGameServer::acceptClients() {
    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
#if defined(__linux__)
        const int clientFd = accept4(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        const int clientFd = accept(listenFd_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
#endif
        if (clientFd >= 0) {
#if !defined(__linux__)
            setFdNonBlocking(clientFd);
            int yes = 1;
#if defined(SO_NOSIGPIPE)
            setsockopt(clientFd, SOL_SOCKET, SO_NOSIGPIPE, &yes, sizeof(yes));
#endif
#endif
            sockets_.emplace(clientFd, ClientSocket{});
            addReadFd(clientFd);
            queueSocketMessage(SocketMessage{SocketMessageType::Accept, clientFd, {}});
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        throwErrno("accept");
    }
}

void WorkerGameServer::readClient(int fd) {
    char buffer[4096];
    while (true) {
        const ssize_t n = recv(fd, buffer, sizeof(buffer), 0);
        if (n > 0) {
            queueSocketMessage(SocketMessage{
                SocketMessageType::Data,
                fd,
                std::string(buffer, static_cast<std::size_t>(n)),
            });
            continue;
        }

        if (n == 0) {
            if (auto it = sockets_.find(fd); it != sockets_.end()) {
                it->second.closing = true;
                updateFdInterest(fd, false, !it->second.output.empty());
            }
            queueSocketMessage(SocketMessage{SocketMessageType::Close, fd, {}});
            return;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        if (errno == EINTR) {
            continue;
        }

        queueSocketMessage(SocketMessage{SocketMessageType::Error, fd, std::strerror(errno)});
        if (auto it = sockets_.find(fd); it != sockets_.end()) {
            it->second.closing = true;
            updateFdInterest(fd, false, !it->second.output.empty());
        }
        return;
    }
}

void WorkerGameServer::flushClient(int fd) {
    auto it = sockets_.find(fd);
    if (it == sockets_.end()) {
        return;
    }

    std::string& output = it->second.output;
    while (!output.empty()) {
        const ssize_t n = send(fd, output.data(), output.size(), kSendFlags);
        if (n > 0) {
            output.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            updateFdInterest(fd, true, true);
            return;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }

        queueSocketMessage(SocketMessage{SocketMessageType::Error, fd, std::strerror(errno)});
        closeClientNow(fd);
        return;
    }

    if (it->second.closing) {
        closeClientNow(fd);
    } else {
        updateFdInterest(fd, true, false);
    }
}

void WorkerGameServer::closeClientNow(int fd) {
    deleteFd(fd);
    sockets_.erase(fd);
    close(fd);
}

void WorkerGameServer::addReadFd(int fd) {
#if defined(__linux__)
    epoll_event event{};
    event.events = EPOLLIN | EPOLLRDHUP;
    event.data.fd = fd;
    if (epoll_ctl(backendFd_, EPOLL_CTL_ADD, fd, &event) < 0) {
        throwErrno("epoll_ctl ADD");
    }
#else
    struct kevent changes[2]{};
    EV_SET(&changes[0], static_cast<uintptr_t>(fd), EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    EV_SET(&changes[1], static_cast<uintptr_t>(fd), EVFILT_WRITE, EV_ADD | EV_DISABLE, 0, 0, nullptr);
    if (kevent(backendFd_, changes, 2, nullptr, 0, nullptr) < 0) {
        throwErrno("kevent add fd");
    }
#endif
}

void WorkerGameServer::updateFdInterest(int fd, bool read, bool write) {
#if defined(__linux__)
    epoll_event event{};
    event.events = (read ? EPOLLIN | EPOLLRDHUP : 0) | (write ? EPOLLOUT : 0);
    event.data.fd = fd;
    if (epoll_ctl(backendFd_, EPOLL_CTL_MOD, fd, &event) < 0 && errno != EBADF && errno != ENOENT) {
        throwErrno("epoll_ctl MOD");
    }
#else
    struct kevent changes[2]{};
    EV_SET(&changes[0], static_cast<uintptr_t>(fd), EVFILT_READ, read ? EV_ENABLE : EV_DISABLE, 0, 0, nullptr);
    EV_SET(&changes[1], static_cast<uintptr_t>(fd), EVFILT_WRITE, write ? EV_ENABLE : EV_DISABLE, 0, 0, nullptr);
    if (kevent(backendFd_, changes, 2, nullptr, 0, nullptr) < 0 && errno != EBADF && errno != ENOENT) {
        throwErrno("kevent update");
    }
#endif
}

void WorkerGameServer::deleteFd(int fd) {
#if defined(__linux__)
    epoll_ctl(backendFd_, EPOLL_CTL_DEL, fd, nullptr);
#else
    struct kevent changes[2]{};
    EV_SET(&changes[0], static_cast<uintptr_t>(fd), EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    EV_SET(&changes[1], static_cast<uintptr_t>(fd), EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(backendFd_, changes, 2, nullptr, 0, nullptr);
#endif
}
