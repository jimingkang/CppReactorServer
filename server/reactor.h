#pragma once

#include <coroutine>
#include <cstdint>
#include <unordered_map>

class Reactor {
public:
    enum class Interest : std::uint32_t {
        Read,
        Write,
    };

    struct EventAwaiter {
        Reactor& reactor;
        int fd;
        Interest interest;

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> handle);
        void await_resume() const noexcept {}
    };

    Reactor();
    ~Reactor();

    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;

    EventAwaiter readable(int fd) noexcept;
    EventAwaiter writable(int fd) noexcept;

    void run();
    void stop() noexcept;
    int backendFd() const noexcept;

private:
    friend struct EventAwaiter;

    void addOrUpdate(int fd, Interest interest, std::coroutine_handle<> handle);
    void removeWaiter(int fd, std::uint32_t events) noexcept;

    int backendFd_ = -1;
    bool running_ = true;
    std::unordered_map<int, std::coroutine_handle<>> readWaiters_;
    std::unordered_map<int, std::coroutine_handle<>> writeWaiters_;
};
