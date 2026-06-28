#pragma once

#include "worker_protocol.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

struct SessionActions {
    std::vector<SkynetMessage> serviceMessages;
    std::vector<SocketCommand> socketCommands;
    bool eraseSession = false;
};

class ISessionAgent {
public:
    virtual ~ISessionAgent() = default;

    virtual int fd() const noexcept = 0;
    virtual int playerId() const noexcept = 0;
    virtual bool closing() const noexcept = 0;

    virtual SessionActions onAccept() = 0;
    virtual SessionActions onSocketData(std::string_view chunk) = 0;
    virtual SessionActions onDisconnect() noexcept = 0;
    virtual SessionActions onWorldResponse(const GameResponse& response) noexcept = 0;
    virtual SessionActions onLoginResponse(const LoginMessage& response) noexcept = 0;
};
