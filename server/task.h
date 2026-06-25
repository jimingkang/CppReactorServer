#pragma once

#include <coroutine>
#include <exception>

class Task {
public:
    struct promise_type {
        Task get_return_object() noexcept {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_never initial_suspend() noexcept {
            return {};
        }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            void await_suspend(std::coroutine_handle<promise_type> handle) noexcept {
                handle.destroy();
            }
            void await_resume() noexcept {}
        };

        FinalAwaiter final_suspend() noexcept {
            return {};
        }

        void return_void() noexcept {}

        void unhandled_exception() {
            exception = std::current_exception();
        }

        std::exception_ptr exception;
    };

    explicit Task(std::coroutine_handle<promise_type> handle = {}) noexcept : handle_(handle) {}

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    Task(Task&& other) noexcept : handle_(other.handle_) {
        other.handle_ = {};
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            handle_ = other.handle_;
            other.handle_ = {};
        }
        return *this;
    }

    // Detached task: coroutine owns itself and is destroyed in final_suspend.
    ~Task() = default;

private:
    std::coroutine_handle<promise_type> handle_;
};
