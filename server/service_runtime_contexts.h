#pragma once

#include "i_game_server_binding.h"
#include "request_reply_service_context.h"
#include "service_context.h"

#include <coroutine>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

class WorkerGameServer;

class CoroutineServiceContext : public ServiceContext {
public:
    explicit CoroutineServiceContext(ServiceId id);
    ~CoroutineServiceContext() override;

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;

protected:
    struct Task {
        struct promise_type {
            Task get_return_object() noexcept;
            std::suspend_always initial_suspend() noexcept;
            std::suspend_always final_suspend() noexcept;
            void unhandled_exception();
            void return_void() noexcept;
        };

        explicit Task(std::coroutine_handle<promise_type> handle = {}) noexcept;
        Task(Task&& other) noexcept;
        Task& operator=(Task&& other) noexcept;
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        ~Task();

        void resume() const;
        bool done() const noexcept;

        std::coroutine_handle<promise_type> handle;
    };

    struct NextMessageAwaiter {
        CoroutineServiceContext& owner;

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        SkynetMessage await_resume();
    };

    virtual Task mainLoop() = 0;
    NextMessageAwaiter nextMessage() noexcept;

    WorkerGameServer& server() const;
    void sendToService(ServiceId destination, SkynetMessage message);
    void logText(std::string text);

private:
    void ensureMainLoop();
    void enqueueMessage(SkynetMessage message);
    void pump();

    WorkerGameServer* server_ = nullptr;
    Task mainLoopTask_;
    std::deque<SkynetMessage> inbox_;
    std::coroutine_handle<> nextMessageWaiter_;
    bool pumping_ = false;
};

class LoggerServiceContext final : public CoroutineServiceContext {
public:
    LoggerServiceContext();

protected:
    Task mainLoop() override;
};

class GateServiceContext final : public ServiceContext {
public:
    GateServiceContext();

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;
};

class ConnectionServiceContext final : public ServiceContext {
public:
    ConnectionServiceContext();

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;
};

class GameWorldServiceContext final : public RequestReplyServiceContext {
public:
    GameWorldServiceContext();

protected:
    Task mainLoop() override;
};

class RoomServiceContext final : public CoroutineServiceContext {
public:
    RoomServiceContext();

protected:
    Task mainLoop() override;
};

class HallServiceContext final : public RequestReplyServiceContext {
public:
    HallServiceContext();

    struct HallRoom {
        int roomId = 0;
        int ownerPlayerId = 0;
        int seatCount = 2;
        std::string gameType;
        std::string roomName;
        std::vector<int> playerIds;
    };

protected:
    Task mainLoop() override;

private:
    struct PendingMatch {
        int fd = -1;
        int playerId = 0;
        int seatCount = 3;
        std::string gameType;
        std::string roomName;
        ServiceId replyService = ServiceId::Connection;
        std::uint64_t replyTo = 0;
    };

    using MatchKey = std::pair<std::string, int>;

    void replyMatchResult(const PendingMatch& pending, const HallRoom& room);
    void tryBuildMatch(const MatchKey& key);

    int nextRoomId_ = 1;
    std::unordered_map<int, HallRoom> rooms_;
    std::map<MatchKey, std::deque<PendingMatch>> pendingMatches_;
};

class LoginServiceContext final : public RequestReplyServiceContext {
public:
    LoginServiceContext();

protected:
    Task mainLoop() override;
};

class DbServiceContext final : public CoroutineServiceContext {
public:
    explicit DbServiceContext(std::vector<UserCredential> seedUsers);

protected:
    Task mainLoop() override;

private:
    std::unordered_map<std::string, std::string> userCredentials_;
};

class RedisServiceContext final : public CoroutineServiceContext {
public:
    RedisServiceContext();

protected:
    Task mainLoop() override;

private:
    std::unordered_map<std::string, std::string> keyValues_;
};
