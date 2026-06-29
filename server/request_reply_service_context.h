#pragma once

#include "service_context.h"

#include <coroutine>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>

class WorkerGameServer;

class RequestReplyServiceContext : public ServiceContext {
public:
    explicit RequestReplyServiceContext(ServiceId id);
    ~RequestReplyServiceContext() override;

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;

protected:
    struct Task {
        struct promise_type {
            struct FinalAwaiter {
                bool await_ready() const noexcept { return false; }
                std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) const noexcept;
                void await_resume() const noexcept {}
            };

            Task get_return_object() noexcept;
            std::suspend_always initial_suspend() noexcept;
            FinalAwaiter final_suspend() noexcept;
            void unhandled_exception();
            void return_void() noexcept;

            std::coroutine_handle<> continuation;
        };

        struct Awaiter {
            std::coroutine_handle<promise_type> handle;

            bool await_ready() const noexcept;
            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) const noexcept;
            void await_resume() const noexcept {}
        };

        explicit Task(std::coroutine_handle<promise_type> handle = {}) noexcept;
        Task(Task&& other) noexcept;
        Task& operator=(Task&& other) noexcept;
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        ~Task();

        void resume() const;
        bool done() const noexcept;
        Awaiter operator co_await() const noexcept;

        std::coroutine_handle<promise_type> handle;
    };

    struct NextMessageAwaiter {
        RequestReplyServiceContext& owner;

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle) noexcept;
        SkynetMessage await_resume();
    };

    struct ServiceCallAwaiter {
        RequestReplyServiceContext& owner;
        SkynetMessage request;
        std::uint64_t requestId = 0;

        bool await_ready() const noexcept;
        bool await_suspend(std::coroutine_handle<> handle);
        SkynetMessage await_resume();
    };

    virtual Task mainLoop() = 0;
    virtual bool shouldStop() const noexcept;

    NextMessageAwaiter nextMessage() noexcept;
    ServiceCallAwaiter callService(SkynetMessage request);
    WorkerGameServer& server() const;

private:
    struct CallState {
        std::coroutine_handle<> handle;
        std::optional<SkynetMessage> response;
    };

    void ensureMainLoop();
    void enqueueMessage(SkynetMessage message);
    bool tryResumePendingCall(const SkynetMessage& message);
    void pump();

    WorkerGameServer* server_ = nullptr;
    Task mainLoop_;
    std::deque<SkynetMessage> inbox_;
    std::coroutine_handle<> nextMessageWaiter_;
    std::unordered_map<std::uint64_t, CallState> pendingCalls_;
    std::uint64_t nextRequestId_ = 1;
    bool awaitingResponse_ = false;
    bool pumping_ = false;
};
