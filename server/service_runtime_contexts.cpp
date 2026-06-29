#include "service_runtime_contexts.h"

#include "worker_server.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

CoroutineServiceContext::CoroutineServiceContext(ServiceId id) : ServiceContext(id) {}

CoroutineServiceContext::~CoroutineServiceContext() = default;

void CoroutineServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    server_ = &server;
    ensureMainLoop();
    enqueueMessage(message);
    pump();
}

CoroutineServiceContext::Task CoroutineServiceContext::Task::promise_type::get_return_object() noexcept {
    return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
}

std::suspend_always CoroutineServiceContext::Task::promise_type::initial_suspend() noexcept {
    return {};
}

std::suspend_always CoroutineServiceContext::Task::promise_type::final_suspend() noexcept {
    return {};
}

void CoroutineServiceContext::Task::promise_type::unhandled_exception() {
    std::rethrow_exception(std::current_exception());
}

void CoroutineServiceContext::Task::promise_type::return_void() noexcept {}

CoroutineServiceContext::Task::Task(std::coroutine_handle<promise_type> handle) noexcept
    : handle(handle) {}

CoroutineServiceContext::Task::Task(Task&& other) noexcept
    : handle(std::exchange(other.handle, {})) {}

auto CoroutineServiceContext::Task::operator=(Task&& other) noexcept -> Task& {
    if (this == &other) {
        return *this;
    }
    if (handle) {
        handle.destroy();
    }
    handle = std::exchange(other.handle, {});
    return *this;
}

CoroutineServiceContext::Task::~Task() {
    if (handle) {
        handle.destroy();
    }
}

void CoroutineServiceContext::Task::resume() const {
    if (handle && !handle.done()) {
        handle.resume();
    }
}

bool CoroutineServiceContext::Task::done() const noexcept {
    return !handle || handle.done();
}

auto CoroutineServiceContext::nextMessage() noexcept -> NextMessageAwaiter {
    return NextMessageAwaiter{*this};
}

WorkerGameServer& CoroutineServiceContext::server() const {
    if (server_ == nullptr) {
        throw std::logic_error("CoroutineServiceContext has no server");
    }
    return *server_;
}

void CoroutineServiceContext::sendToService(ServiceId destination, SkynetMessage message) {
    server().sendToService(destination, std::move(message));
}

void CoroutineServiceContext::logText(std::string text) {
    server().logText(std::move(text));
}

void CoroutineServiceContext::ensureMainLoop() {
    if (mainLoopTask_.handle) {
        return;
    }
    mainLoopTask_ = mainLoop();
}

void CoroutineServiceContext::enqueueMessage(SkynetMessage message) {
    inbox_.push_back(std::move(message));
}

void CoroutineServiceContext::pump() {
    if (pumping_ || !mainLoopTask_.handle || mainLoopTask_.done()) {
        return;
    }
    pumping_ = true;
    while (!mainLoopTask_.done()) {
        if (nextMessageWaiter_ && inbox_.empty()) {
            break;
        }
        if (inbox_.empty()) {
            break;
        }
        mainLoopTask_.resume();
    }
    pumping_ = false;
}

bool CoroutineServiceContext::NextMessageAwaiter::await_ready() const noexcept {
    return !owner.inbox_.empty();
}

void CoroutineServiceContext::NextMessageAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
    owner.nextMessageWaiter_ = handle;
}

SkynetMessage CoroutineServiceContext::NextMessageAwaiter::await_resume() {
    owner.nextMessageWaiter_ = {};
    SkynetMessage message = std::move(owner.inbox_.front());
    owner.inbox_.pop_front();
    return message;
}

LoggerServiceContext::LoggerServiceContext() : CoroutineServiceContext(ServiceId::Logger) {}

auto LoggerServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind == MessageKind::Log && !message.text.empty()) {
            std::cout << message.text << '\n';
        }
    }
}

GateServiceContext::GateServiceContext() : ServiceContext(ServiceId::Gate) {}

void GateServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    server.handleGateService(message);
}

ConnectionServiceContext::ConnectionServiceContext() : ServiceContext(ServiceId::Connection) {}

void ConnectionServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    server.handleConnectionService(message);
}

GameWorldServiceContext::GameWorldServiceContext() : RequestReplyServiceContext(ServiceId::GameWorld) {}

auto GameWorldServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        server().handleGameWorldService(message);
    }
}

RoomServiceContext::RoomServiceContext() : CoroutineServiceContext(ServiceId::Room) {}

auto RoomServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::Room) {
            continue;
        }
        // RoomService is a placeholder for room/map sharding. It intentionally has
        // its own queue so workers can schedule it independently from GameWorld.
    }
}

LoginServiceContext::LoginServiceContext() : RequestReplyServiceContext(ServiceId::Login) {}

auto LoginServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();

        if (message.kind == MessageKind::Login) {
            const LoginMessage& login = message.login;
            if (login.type != LoginMessageType::Request) {
                continue;
            }

            SkynetMessage dbRequest;
            dbRequest.source = ServiceId::Login;
            dbRequest.kind = MessageKind::Db;
            dbRequest.destination = ServiceId::Db;
            dbRequest.db.type = DbMessageType::CheckCredentials;
            dbRequest.db.fd = login.fd;
            dbRequest.db.username = login.username;
            dbRequest.db.password = login.password;
            SkynetMessage dbResultMessage = co_await callService(std::move(dbRequest));
            const DbMessage& db = dbResultMessage.db;

            SkynetMessage response;
            response.source = ServiceId::Login;
            response.kind = MessageKind::Login;
            response.replyTo = message.requestId;
            response.login.type = LoginMessageType::Result;
            response.login.fd = db.fd;
            response.login.username = db.username;
            response.login.success = db.success;
            response.login.reason = db.reason;
            server().sendToService(ServiceId::Connection, std::move(response));

            server().logText(std::string("login ") + (db.success ? "ok" : "fail") + " user=" + db.username + " fd=" + std::to_string(db.fd));
            continue;
        }
    }
}

DbServiceContext::DbServiceContext(std::vector<UserCredential> seedUsers)
    : CoroutineServiceContext(ServiceId::Db) {
    for (auto& user : seedUsers) {
        userCredentials_.emplace(std::move(user.username), std::move(user.password));
    }
}

auto DbServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::Db) {
            continue;
        }

        const DbMessage& db = message.db;
        if (db.type == DbMessageType::SavePlayer) {
            logText("save_player player=" + std::to_string(db.playerId));
            continue;
        }
        if (db.type != DbMessageType::CheckCredentials) {
            continue;
        }

        SkynetMessage result;
        result.source = ServiceId::Db;
        result.kind = MessageKind::Db;
        result.destination = ServiceId::Login;
        result.replyTo = message.requestId;
        result.db.type = DbMessageType::CredentialsResult;
        result.db.fd = db.fd;
        result.db.username = db.username;

        const auto it = userCredentials_.find(db.username);
        if (it == userCredentials_.end()) {
            result.db.success = false;
            result.db.reason = "unknown_user";
        } else if (it->second != db.password) {
            result.db.success = false;
            result.db.reason = "bad_password";
        } else {
            result.db.success = true;
            result.db.reason = "ok";
        }

        sendToService(ServiceId::Login, std::move(result));
    }
}
