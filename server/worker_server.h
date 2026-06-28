#pragma once

#include "worker_protocol.h"
#include "supermario/super_mario_game_world.h"
#include "supermario/super_mario_session_agent.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class ServiceQueue {
public:
    explicit ServiceQueue(ServiceId id);

    ServiceId id() const noexcept;
    bool push(SkynetMessage message);
    bool popOne(SkynetMessage& message);
    bool finishBatch();

private:
    ServiceId id_;
    std::mutex mutex_;
    std::queue<SkynetMessage> queue_;
    bool scheduled_ = false;
};

enum class SocketCommandType {
    Send,
    Close,
};

struct SocketCommand {
    SocketCommandType type = SocketCommandType::Send;
    int fd = -1;
    std::string data;
};

class WorkerGameServer {
public:
    WorkerGameServer(std::string host, int port, std::size_t workerCount);
    ~WorkerGameServer();

    WorkerGameServer(const WorkerGameServer&) = delete;
    WorkerGameServer& operator=(const WorkerGameServer&) = delete;

    void run();
    void stop();

private:
    struct ClientSocket {
        std::string output;
        bool closing = false;
    };

    void socketLoop();
    void workerLoop(std::size_t workerId);

    void sendToService(ServiceId destination, SkynetMessage message);
    ServiceQueue* waitReadyService();
    void rescheduleService(ServiceQueue& queue);
    ServiceQueue& serviceQueue(ServiceId id);

    void dispatchMessage(const SkynetMessage& message);
    void handleLoggerService(const SkynetMessage& message);
    void handleGateService(const SkynetMessage& message);
    void handleConnectionService(const SkynetMessage& message);
    void handleGameWorldService(const SkynetMessage& message);
    void handleRoomService(const SkynetMessage& message);
    void handleDbService(const SkynetMessage& message);

    void queueSocketMessage(SocketMessage message);
    void queueSocketCommand(SocketCommand command);
    void drainSocketCommands();
    void wakeSocketLoop();

    void acceptClients();
    void readClient(int fd);
    void flushClient(int fd);
    void closeClientNow(int fd);

    void addReadFd(int fd);
    void updateFdInterest(int fd, bool read, bool write);
    void deleteFd(int fd);

    std::string host_;
    int port_ = 0;
    std::size_t workerCount_ = 0;

    std::atomic_bool running_{false};
    int listenFd_ = -1;
    int backendFd_ = -1;
    int wakeReadFd_ = -1;
    int wakeWriteFd_ = -1;

    std::vector<std::unique_ptr<ServiceQueue>> services_;
    std::mutex globalMutex_;
    std::condition_variable globalReady_;
    std::queue<ServiceQueue*> globalQueue_;
    bool servicesStopped_ = false;
    std::vector<std::thread> workers_;
    std::thread tickThread_;

    std::mutex commandMutex_;
    std::queue<SocketCommand> socketCommands_;
    std::unordered_map<int, ClientSocket> sockets_;

    supermario::SuperMarioGameWorld world_;
    std::unordered_map<int, supermario::SuperMarioSessionAgent> sessions_;
};
