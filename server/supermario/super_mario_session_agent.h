#pragma once

#include "../worker_protocol.h"

#include <string>
#include <string_view>
#include <vector>

namespace supermario {

struct DisconnectPlan {
    bool sendLeaveToWorld = false;
    int playerId = 0;
    bool eraseSession = false;
    bool closeSocket = true;
};

struct ResponsePlan {
    std::string outboundText;
    bool eraseSession = false;
    bool closeSocket = false;
};

class SuperMarioSessionAgent {
public:
    explicit SuperMarioSessionAgent(int fd);

    int fd() const noexcept;
    int playerId() const noexcept;
    bool closing() const noexcept;

    std::vector<GameCommand> onSocketData(std::string_view chunk);
    std::string acceptJoin(int playerId, std::string snapshot);
    DisconnectPlan beginDisconnect() noexcept;
    ResponsePlan onWorldResponse(const GameResponse& response) noexcept;
    GameCommand makeJoinCommand() const;
    GameCommand makeLeaveCommand() const;

private:
    static std::string ensureNewline(std::string data);
    static std::string trimLine(std::string line);

    int fd_ = -1;
    int playerId_ = 0;
    bool closing_ = false;
    std::string input_;
};

} // namespace supermario
