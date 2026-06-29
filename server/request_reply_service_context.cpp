#include "request_reply_service_context.h"

#include "worker_server.h"

#include <exception>
#include <stdexcept>
#include <utility>

RequestReplyServiceContext::RequestReplyServiceContext(ServiceId id) : ServiceContext(id) {}

RequestReplyServiceContext::~RequestReplyServiceContext() = default;

void RequestReplyServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    if (shouldStop()) {
        return;
    }

    server_ = &server;
    ensureMainLoop();

    if (!tryResumePendingCall(message)) {
        enqueueMessage(message);
    }
    pump();
}

RequestReplyServiceContext::Task RequestReplyServiceContext::Task::promise_type::get_return_object() noexcept {
    return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
}

std::suspend_always RequestReplyServiceContext::Task::promise_type::initial_suspend() noexcept {
    return {};
}

auto RequestReplyServiceContext::Task::promise_type::FinalAwaiter::await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
    -> std::coroutine_handle<> {
    return handle.promise().continuation;
}

RequestReplyServiceContext::Task::promise_type::FinalAwaiter RequestReplyServiceContext::Task::promise_type::final_suspend() noexcept {
    return {};
}

void RequestReplyServiceContext::Task::promise_type::unhandled_exception() {
    std::rethrow_exception(std::current_exception());
}

void RequestReplyServiceContext::Task::promise_type::return_void() noexcept {}

RequestReplyServiceContext::Task::Task(std::coroutine_handle<promise_type> handle) noexcept
    : handle(handle) {}

RequestReplyServiceContext::Task::Task(Task&& other) noexcept
    : handle(std::exchange(other.handle, {})) {}

auto RequestReplyServiceContext::Task::operator=(Task&& other) noexcept -> Task& {
    if (this == &other) {
        return *this;
    }
    if (handle) {
        handle.destroy();
    }
    handle = std::exchange(other.handle, {});
    return *this;
}

RequestReplyServiceContext::Task::~Task() {
    if (handle) {
        handle.destroy();
    }
}

void RequestReplyServiceContext::Task::resume() const {
    if (handle && !handle.done()) {
        handle.resume();
    }
}

bool RequestReplyServiceContext::Task::done() const noexcept {
    return !handle || handle.done();
}

bool RequestReplyServiceContext::Task::Awaiter::await_ready() const noexcept {
    return !handle || handle.done();
}

auto RequestReplyServiceContext::Task::Awaiter::await_suspend(std::coroutine_handle<> continuation) const noexcept
    -> std::coroutine_handle<> {
    handle.promise().continuation = continuation;
    return handle;
}

RequestReplyServiceContext::Task::Awaiter RequestReplyServiceContext::Task::operator co_await() const noexcept {
    return Awaiter{handle};
}

bool RequestReplyServiceContext::shouldStop() const noexcept {
    return false;
}

auto RequestReplyServiceContext::nextMessage() noexcept -> NextMessageAwaiter {
    return NextMessageAwaiter{*this};
}

auto RequestReplyServiceContext::callService(SkynetMessage request) -> ServiceCallAwaiter {
    return ServiceCallAwaiter{*this, std::move(request)};
}

WorkerGameServer& RequestReplyServiceContext::server() const {
    if (server_ == nullptr) {
        throw std::logic_error("RequestReplyServiceContext has no server");
    }
    return *server_;
}

void RequestReplyServiceContext::ensureMainLoop() {
    if (mainLoop_.handle) {
        return;
    }
    mainLoop_ = mainLoop();
}

void RequestReplyServiceContext::enqueueMessage(SkynetMessage message) {
    inbox_.push_back(std::move(message));
}

bool RequestReplyServiceContext::tryResumePendingCall(const SkynetMessage& message) {
    if (message.replyTo == 0) {
        return false;
    }

    const auto it = pendingCalls_.find(message.replyTo);
    if (it == pendingCalls_.end()) {
        return false;
    }

    it->second.response = message;
    if (it->second.handle) {
        it->second.handle.resume();
    }
    return true;
}

void RequestReplyServiceContext::pump() {
    if (pumping_ || !mainLoop_.handle || mainLoop_.done()) {
        return;
    }
    pumping_ = true;
    while (!mainLoop_.done()) {
        if (nextMessageWaiter_ && inbox_.empty()) {
            break;
        }
        bool hasPendingResponse = false;
        for (const auto& [_, call] : pendingCalls_) {
            if (call.response.has_value()) {
                hasPendingResponse = true;
                break;
            }
        }
        if (awaitingResponse_) {
            if (!hasPendingResponse) {
                break;
            }
        } else if (inbox_.empty()) {
            break;
        }
        mainLoop_.resume();
    }
    pumping_ = false;
}

bool RequestReplyServiceContext::NextMessageAwaiter::await_ready() const noexcept {
    return !owner.inbox_.empty();
}

void RequestReplyServiceContext::NextMessageAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    owner.nextMessageWaiter_ = handle;
}

SkynetMessage RequestReplyServiceContext::NextMessageAwaiter::await_resume() {
    owner.nextMessageWaiter_ = {};
    SkynetMessage message = std::move(owner.inbox_.front());
    owner.inbox_.pop_front();
    return message;
}

bool RequestReplyServiceContext::ServiceCallAwaiter::await_ready() const noexcept {
    return false;
}

bool RequestReplyServiceContext::ServiceCallAwaiter::await_suspend(std::coroutine_handle<> handle) {
    requestId = owner.nextRequestId_++;
    request.requestId = requestId;
    request.replyTo = 0;
    owner.awaitingResponse_ = true;
    owner.pendingCalls_.emplace(requestId, CallState{handle, std::nullopt});
    owner.server().sendToService(request.destination, std::move(request));
    return true;
}

SkynetMessage RequestReplyServiceContext::ServiceCallAwaiter::await_resume() {
    const auto it = owner.pendingCalls_.find(requestId);
    if (it == owner.pendingCalls_.end() || !it->second.response.has_value()) {
        throw std::logic_error("Missing coroutine response");
    }
    SkynetMessage response = std::move(*it->second.response);
    owner.pendingCalls_.erase(it);
    owner.awaitingResponse_ = false;
    return response;
}
