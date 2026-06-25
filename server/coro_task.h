#pragma once

#include <coroutine>
#include <exception>

// Fire-and-forget coroutine task.
// The coroutine starts immediately, suspends on I/O awaiters, and destroys itself at final_suspend.
class DetachedTask {
public:
    struct promise_type {
        DetachedTask get_return_object() noexcept {
            return DetachedTask{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        struct FinalSuspend {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                handle.destroy();
            }
            void await_resume() noexcept {}
        };

        FinalSuspend final_suspend() noexcept {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() noexcept {
            exception = std::current_exception();
        }

        std::exception_ptr exception;
    };

    explicit DetachedTask(std::coroutine_handle<promise_type> handle = {}) noexcept : handle_(handle) {}

    DetachedTask(const DetachedTask&) = delete;
    DetachedTask& operator=(const DetachedTask&) = delete;

    DetachedTask(DetachedTask&& other) noexcept : handle_(other.handle_) {
        other.handle_ = {};
    }

    DetachedTask& operator=(DetachedTask&& other) noexcept {
        if (this != &other) {
            handle_ = other.handle_;
            other.handle_ = {};
        }
        return *this;
    }

    ~DetachedTask() = default;

private:
    std::coroutine_handle<promise_type> handle_;
};
