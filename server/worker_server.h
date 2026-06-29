#pragma once

#include "i_game_server_binding.h"
#include "service_context.h"
#include "service_runtime_contexts.h"
#include "session_service_context.h"
#include "worker_protocol.h"

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

class WorkerGameServer {
public:
    WorkerGameServer(std::string host, int port, std::size_t workerCount, std::unique_ptr<IGameServerBinding> binding);
    ~WorkerGameServer();

    WorkerGameServer(const WorkerGameServer&) = delete;
    WorkerGameServer& operator=(const WorkerGameServer&) = delete;

    void applySessionActions(const SessionActions& actions);
    void removeSessionContext(int fd);
    void sendToService(ServiceId destination, SkynetMessage message);
    void logText(std::string text);

    void run();
    void stop();

private:
    friend class GateServiceContext;
    friend class ConnectionServiceContext;
    friend class GameWorldServiceContext;
    friend class LoggerServiceContext;
    friend class LoginServiceContext;
    friend class DbServiceContext;
    friend class RoomServiceContext;
    friend class HallServiceContext;
    friend class RedisServiceContext;

    struct ClientSocket {
        std::string output;
        bool closing = false;
    };

    void socketLoop();
    void workerLoop(std::size_t workerId);

    void handleGateService(const SkynetMessage& message);
    void handleConnectionService(const SkynetMessage& message);
    void handleGameWorldService(const SkynetMessage& message);
    void sendToContext(ServiceContext& context, SkynetMessage message);
    ServiceContext* waitReadyService();
    void rescheduleService(ServiceContext& context);
    bool collectFinishedSession(ServiceContext& context);
    ServiceContext& serviceContext(ServiceId id);
    SessionServiceContext* sessionContext(int fd);

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

    std::vector<std::unique_ptr<ServiceContext>> services_;
    std::mutex globalMutex_;
    std::condition_variable globalReady_;
    std::queue<ServiceContext*> globalQueue_;
    bool servicesStopped_ = false;
    std::vector<std::thread> workers_;
    std::thread tickThread_;

    std::mutex commandMutex_;
    std::queue<SocketCommand> socketCommands_;
    std::unordered_map<int, ClientSocket> sockets_;

    std::unique_ptr<IGameServerBinding> binding_;
    std::unique_ptr<IGameWorld> world_;
    std::unordered_map<int, std::unique_ptr<SessionServiceContext>> sessions_;
};
