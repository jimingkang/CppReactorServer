#pragma once

#include <coroutine>
#include <cstdint>
#include <unordered_map>

class CoroReactor {
public:
    enum class EventKind : std::uint32_t {
        Read,
        Write,
    };

    class IoAwaiter {
    public:
        IoAwaiter(CoroReactor& reactor, int fd, EventKind kind) noexcept;

        bool await_ready() const noexcept;
        void await_suspend(std::coroutine_handle<> coroutine);
        void await_resume() const noexcept;

    private:
        CoroReactor& reactor_;
        int fd_ = -1;
        EventKind kind_ = EventKind::Read;
    };

    CoroReactor();
    ~CoroReactor();

    CoroReactor(const CoroReactor&) = delete;
    CoroReactor& operator=(const CoroReactor&) = delete;

    IoAwaiter waitReadable(int fd) noexcept;
    IoAwaiter waitWritable(int fd) noexcept;

    void run();
    void stop() noexcept;

private:
    friend class IoAwaiter;

    void arm(int fd, EventKind kind, std::coroutine_handle<> coroutine);
    void disarmDelivered(int fd, bool readable, bool writable) noexcept;

    int eventFd_ = -1;
    bool running_ = true;
    std::unordered_map<int, std::coroutine_handle<>> readCoroutines_;
    std::unordered_map<int, std::coroutine_handle<>> writeCoroutines_;
};
