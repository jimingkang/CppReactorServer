#pragma once

#include "i_game_server_binding.h"
#include "service_context.h"

#include <memory>
#include <unordered_map>
#include <vector>

class WorkerGameServer;

class LoggerServiceContext final : public ServiceContext {
public:
    LoggerServiceContext();

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;
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

class GameWorldServiceContext final : public ServiceContext {
public:
    GameWorldServiceContext();

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;
};

class RoomServiceContext final : public ServiceContext {
public:
    RoomServiceContext();

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;
};

class LoginServiceContext final : public ServiceContext {
public:
    LoginServiceContext();

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;
};

class DbServiceContext final : public ServiceContext {
public:
    explicit DbServiceContext(std::vector<UserCredential> seedUsers);

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;

private:
    std::unordered_map<std::string, std::string> userCredentials_;
};
