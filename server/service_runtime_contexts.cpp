#include "service_runtime_contexts.h"

#include "worker_server.h"

#include <iostream>
#include <utility>

LoggerServiceContext::LoggerServiceContext() : ServiceContext(ServiceId::Logger) {}

void LoggerServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    (void)server;
    if (message.kind == MessageKind::Log && !message.text.empty()) {
        std::cout << message.text << '\n';
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

GameWorldServiceContext::GameWorldServiceContext() : ServiceContext(ServiceId::GameWorld) {}

void GameWorldServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    server.handleGameWorldService(message);
}

RoomServiceContext::RoomServiceContext() : ServiceContext(ServiceId::Room) {}

void RoomServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    (void)server;
    if (message.kind != MessageKind::Room) {
        return;
    }
    // RoomService is a placeholder for room/map sharding. It intentionally has
    // its own queue so workers can schedule it independently from GameWorld.
}

LoginServiceContext::LoginServiceContext() : ServiceContext(ServiceId::Login) {}

void LoginServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    if (message.kind == MessageKind::Login) {
        const LoginMessage& login = message.login;
        if (login.type != LoginMessageType::Request) {
            return;
        }

        SkynetMessage db;
        db.source = ServiceId::Login;
        db.kind = MessageKind::Db;
        db.destination = ServiceId::Db;
        db.db.type = DbMessageType::CheckCredentials;
        db.db.fd = login.fd;
        db.db.username = login.username;
        db.db.password = login.password;
        server.sendToService(ServiceId::Db, std::move(db));
        return;
    }

    if (message.kind != MessageKind::Db) {
        return;
    }

    const DbMessage& db = message.db;
    if (db.type != DbMessageType::CredentialsResult) {
        return;
    }

    SkynetMessage response;
    response.source = ServiceId::Login;
    response.kind = MessageKind::Login;
    response.login.type = LoginMessageType::Result;
    response.login.fd = db.fd;
    response.login.username = db.username;
    response.login.success = db.success;
    response.login.reason = db.reason;
    server.sendToService(ServiceId::Connection, std::move(response));

    server.logText(std::string("login ") + (db.success ? "ok" : "fail") + " user=" + db.username + " fd=" + std::to_string(db.fd));
}

DbServiceContext::DbServiceContext(std::vector<UserCredential> seedUsers)
    : ServiceContext(ServiceId::Db) {
    for (auto& user : seedUsers) {
        userCredentials_.emplace(std::move(user.username), std::move(user.password));
    }
}

void DbServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    if (message.kind != MessageKind::Db) {
        return;
    }

    const DbMessage& db = message.db;
    if (db.type == DbMessageType::SavePlayer) {
        server.logText("save_player player=" + std::to_string(db.playerId));
        return;
    }
    if (db.type != DbMessageType::CheckCredentials) {
        return;
    }

    SkynetMessage result;
    result.source = ServiceId::Db;
    result.kind = MessageKind::Db;
    result.destination = ServiceId::Login;
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

    server.sendToService(ServiceId::Login, std::move(result));
}
