#include "service_runtime_contexts.h"

#include "worker_server.h"

#include <exception>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace {

std::string trimRedisValue(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return value;
}

std::string hallRoomKey(int roomId) {
    return "hall:room:" + std::to_string(roomId);
}

std::string hallIndexKey() {
    return "hall:index";
}

std::string hallQueueKey(std::string_view gameType, int seatCount) {
    return "hall:queue:" + std::string(gameType) + ":" + std::to_string(seatCount);
}

std::string joinInts(const std::vector<int>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << values[i];
    }
    return out.str();
}

std::string hallRoomSummary(const HallServiceContext::HallRoom& room) {
    std::ostringstream out;
    out << "room=" << room.roomId
        << " game=" << room.gameType
        << " name=" << room.roomName
        << " owner=" << room.ownerPlayerId
        << " seats=" << room.playerIds.size() << '/' << room.seatCount
        << " players=" << joinInts(room.playerIds);
    return out.str();
}

std::string hallSnapshot(const std::unordered_map<int, HallServiceContext::HallRoom>& rooms) {
    if (rooms.empty()) {
        return "rooms=0";
    }

    std::vector<int> roomIds;
    roomIds.reserve(rooms.size());
    for (const auto& [roomId, _] : rooms) {
        roomIds.push_back(roomId);
    }
    std::sort(roomIds.begin(), roomIds.end());

    std::ostringstream out;
    out << "rooms=" << roomIds.size();
    for (int roomId : roomIds) {
        out << '\n' << hallRoomSummary(rooms.at(roomId));
    }
    return out.str();
}

SkynetMessage makeRedisRequest(RedisMessageType type, std::string key, std::string value = {}, std::string prefix = {}) {
    SkynetMessage message;
    message.source = ServiceId::Hall;
    message.destination = ServiceId::Redis;
    message.kind = MessageKind::Redis;
    message.redis.type = type;
    message.redis.key = std::move(key);
    message.redis.value = std::move(value);
    message.redis.prefix = std::move(prefix);
    return message;
}

} // namespace

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

HallServiceContext::HallServiceContext() : RequestReplyServiceContext(ServiceId::Hall) {}

auto HallServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::Hall) {
            continue;
        }

        const HallMessage& hall = message.hall;
        HallMessage result;
        result.type = HallMessageType::Result;
        result.fd = hall.fd;
        result.playerId = hall.playerId;
        result.roomId = hall.roomId;
        result.gameType = hall.gameType;
        result.roomName = hall.roomName;

        switch (hall.type) {
        case HallMessageType::ListRooms: {
            result.success = true;
            result.payload = hallSnapshot(rooms_);
            break;
        }
        case HallMessageType::AutoMatch: {
            if (hall.playerId <= 0) {
                result.success = false;
                result.reason = "invalid_player";
                break;
            }

            const MatchKey key{hall.gameType.empty() ? std::string("board") : hall.gameType, std::max(2, hall.seatCount)};
            auto& queue = pendingMatches_[key];
            queue.push_back(PendingMatch{
                hall.fd,
                hall.playerId,
                key.second,
                key.first,
                hall.roomName,
                message.source,
                message.requestId,
            });
            co_await callService(makeRedisRequest(RedisMessageType::Set, hallQueueKey(key.first, key.second), std::to_string(queue.size())));
            tryBuildMatch(key);
            continue;
        }
        case HallMessageType::CancelMatch: {
            bool removed = false;
            for (auto it = pendingMatches_.begin(); it != pendingMatches_.end(); ++it) {
                auto& queue = it->second;
                const auto before = queue.size();
                queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const PendingMatch& pending) {
                    return pending.fd == hall.fd;
                }), queue.end());
                if (queue.size() != before) {
                    removed = true;
                    co_await callService(makeRedisRequest(RedisMessageType::Set, hallQueueKey(it->first.first, it->first.second), std::to_string(queue.size())));
                    if (queue.empty()) {
                        pendingMatches_.erase(it);
                    }
                    break;
                }
            }
            result.success = removed;
            result.reason = removed ? "cancelled" : "not_found";
            break;
        }
        case HallMessageType::CreateRoom: {
            if (hall.playerId <= 0) {
                result.success = false;
                result.reason = "invalid_player";
                break;
            }

            HallRoom room;
            room.roomId = nextRoomId_++;
            room.ownerPlayerId = hall.playerId;
            room.seatCount = std::max(2, hall.seatCount);
            room.gameType = hall.gameType.empty() ? "board" : hall.gameType;
            room.roomName = hall.roomName.empty() ? ("room-" + std::to_string(room.roomId)) : hall.roomName;
            room.playerIds.push_back(hall.playerId);

            result.roomId = room.roomId;
            result.playerIds = room.playerIds;
            result.success = true;
            result.payload = hallRoomSummary(room);
            rooms_.emplace(room.roomId, room);

            co_await callService(makeRedisRequest(RedisMessageType::Set, hallRoomKey(room.roomId), result.payload));
            co_await callService(makeRedisRequest(RedisMessageType::Set, hallIndexKey(), hallSnapshot(rooms_)));
            server().logText("hall create room=" + std::to_string(room.roomId) + " owner=" + std::to_string(hall.playerId));
            break;
        }
        case HallMessageType::JoinRoom: {
            auto it = rooms_.find(hall.roomId);
            if (it == rooms_.end()) {
                result.success = false;
                result.reason = "room_not_found";
                break;
            }
            HallRoom& room = it->second;
            if (hall.playerId <= 0) {
                result.success = false;
                result.reason = "invalid_player";
                break;
            }
            if (std::find(room.playerIds.begin(), room.playerIds.end(), hall.playerId) != room.playerIds.end()) {
                result.success = true;
                result.playerIds = room.playerIds;
                result.payload = hallRoomSummary(room);
                break;
            }
            if (static_cast<int>(room.playerIds.size()) >= room.seatCount) {
                result.success = false;
                result.reason = "room_full";
                break;
            }

            room.playerIds.push_back(hall.playerId);
            result.success = true;
            result.playerIds = room.playerIds;
            result.payload = hallRoomSummary(room);

            co_await callService(makeRedisRequest(RedisMessageType::Set, hallRoomKey(room.roomId), result.payload));
            co_await callService(makeRedisRequest(RedisMessageType::Set, hallIndexKey(), hallSnapshot(rooms_)));
            server().logText("hall join room=" + std::to_string(room.roomId) + " player=" + std::to_string(hall.playerId));
            break;
        }
        case HallMessageType::LeaveRoom: {
            auto it = rooms_.find(hall.roomId);
            if (it == rooms_.end()) {
                result.success = false;
                result.reason = "room_not_found";
                break;
            }
            HallRoom& room = it->second;
            auto playerIt = std::find(room.playerIds.begin(), room.playerIds.end(), hall.playerId);
            if (playerIt == room.playerIds.end()) {
                result.success = false;
                result.reason = "player_not_in_room";
                break;
            }

            room.playerIds.erase(playerIt);
            if (room.playerIds.empty()) {
                rooms_.erase(it);
                result.success = true;
                result.payload = "room_removed";
                co_await callService(makeRedisRequest(RedisMessageType::Delete, hallRoomKey(hall.roomId)));
            } else {
                if (room.ownerPlayerId == hall.playerId) {
                    room.ownerPlayerId = room.playerIds.front();
                }
                result.success = true;
                result.playerIds = room.playerIds;
                result.payload = hallRoomSummary(room);
                co_await callService(makeRedisRequest(RedisMessageType::Set, hallRoomKey(room.roomId), result.payload));
            }
            co_await callService(makeRedisRequest(RedisMessageType::Set, hallIndexKey(), hallSnapshot(rooms_)));
            server().logText("hall leave room=" + std::to_string(hall.roomId) + " player=" + std::to_string(hall.playerId));
            break;
        }
        case HallMessageType::Result:
            continue;
        }

        SkynetMessage response;
        response.source = ServiceId::Hall;
        response.destination = message.source;
        response.kind = MessageKind::Hall;
        response.replyTo = message.requestId;
        response.hall = std::move(result);
        server().sendToService(response.destination, std::move(response));
    }
}

void HallServiceContext::replyMatchResult(const PendingMatch& pending, const HallRoom& room) {
    SkynetMessage response;
    response.source = ServiceId::Hall;
    response.destination = pending.replyService;
    response.kind = MessageKind::Hall;
    response.replyTo = pending.replyTo;
    response.hall.type = HallMessageType::Result;
    response.hall.fd = pending.fd;
    response.hall.playerId = pending.playerId;
    response.hall.roomId = room.roomId;
    response.hall.seatCount = room.seatCount;
    response.hall.success = true;
    response.hall.gameType = room.gameType;
    response.hall.roomName = room.roomName;
    response.hall.payload = hallRoomSummary(room);
    response.hall.playerIds = room.playerIds;
    server().sendToService(response.destination, std::move(response));
}

void HallServiceContext::tryBuildMatch(const MatchKey& key) {
    auto it = pendingMatches_.find(key);
    if (it == pendingMatches_.end()) {
        return;
    }

    auto& queue = it->second;
    if (static_cast<int>(queue.size()) < key.second) {
        return;
    }

    HallRoom room;
    room.roomId = nextRoomId_++;
    room.ownerPlayerId = queue.front().playerId;
    room.seatCount = key.second;
    room.gameType = key.first;
    room.roomName = queue.front().roomName.empty() ? ("room-" + std::to_string(room.roomId)) : queue.front().roomName;

    std::vector<PendingMatch> matched;
    matched.reserve(static_cast<std::size_t>(key.second));
    for (int i = 0; i < key.second; ++i) {
        matched.push_back(queue.front());
        room.playerIds.push_back(queue.front().playerId);
        queue.pop_front();
    }
    rooms_.emplace(room.roomId, room);
    const auto remaining = queue.size();
    if (queue.empty()) {
        pendingMatches_.erase(it);
    }

    server().sendToService(ServiceId::Redis, makeRedisRequest(RedisMessageType::Set, hallRoomKey(room.roomId), hallRoomSummary(room)));
    server().sendToService(ServiceId::Redis, makeRedisRequest(RedisMessageType::Set, hallIndexKey(), hallSnapshot(rooms_)));
    server().sendToService(ServiceId::Redis, makeRedisRequest(RedisMessageType::Set, hallQueueKey(key.first, key.second), std::to_string(remaining)));

    for (const PendingMatch& pending : matched) {
        replyMatchResult(pending, room);
    }
    server().logText("hall automatch room=" + std::to_string(room.roomId) + " game=" + room.gameType + " players=" + joinInts(room.playerIds));
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

RedisServiceContext::RedisServiceContext() : CoroutineServiceContext(ServiceId::Redis) {}

auto RedisServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::Redis) {
            continue;
        }

        const RedisMessage& redis = message.redis;
        RedisMessage result;
        result.type = RedisMessageType::Result;

        switch (redis.type) {
        case RedisMessageType::Get: {
            const auto it = keyValues_.find(redis.key);
            if (it == keyValues_.end()) {
                result.success = false;
                result.reason = "not_found";
            } else {
                result.success = true;
                result.value = it->second;
            }
            break;
        }
        case RedisMessageType::Set:
            keyValues_[redis.key] = trimRedisValue(redis.value);
            result.success = true;
            result.value = keyValues_[redis.key];
            break;
        case RedisMessageType::Delete:
            result.success = keyValues_.erase(redis.key) > 0;
            result.reason = result.success ? "ok" : "not_found";
            break;
        case RedisMessageType::KeysByPrefix:
            result.success = true;
            for (const auto& [key, value] : keyValues_) {
                if (!redis.prefix.empty() && key.rfind(redis.prefix, 0) != 0) {
                    continue;
                }
                result.values.push_back(key + "=" + value);
            }
            std::sort(result.values.begin(), result.values.end());
            break;
        case RedisMessageType::Result:
            continue;
        }

        SkynetMessage response;
        response.source = ServiceId::Redis;
        response.destination = message.source;
        response.kind = MessageKind::Redis;
        response.replyTo = message.requestId;
        response.redis = std::move(result);
        sendToService(response.destination, std::move(response));
    }
}
