#include "super_mario_session_agent.h"

namespace supermario {

SuperMarioSessionAgent::SuperMarioSessionAgent(int fd) : fd_(fd) {}

int SuperMarioSessionAgent::fd() const noexcept {
    return fd_;
}

int SuperMarioSessionAgent::playerId() const noexcept {
    return playerId_;
}

bool SuperMarioSessionAgent::closing() const noexcept {
    return closing_;
}

std::vector<GameCommand> SuperMarioSessionAgent::onSocketData(std::string_view chunk) {
    input_.append(chunk.data(), chunk.size());

    std::vector<GameCommand> commands;
    std::size_t pos = 0;
    while ((pos = input_.find('\n')) != std::string::npos) {
        std::string line = trimLine(input_.substr(0, pos + 1));
        input_.erase(0, pos + 1);
        if (!line.empty()) {
            commands.push_back(GameCommand{GameCommandType::Command, fd_, playerId_, std::move(line)});
        }
    }
    return commands;
}

std::string SuperMarioSessionAgent::acceptJoin(int playerId, std::string snapshot) {
    playerId_ = playerId;

    std::string payload = ensureNewline("WELCOME player=" + std::to_string(playerId_));
    payload += "COMMANDS INPUT seq vx vy | MOVE dx dy | POS x y | ATTACK playerId | COIN coinId | STATE | PING | QUIT\n";
    payload += std::move(snapshot);
    return payload;
}

DisconnectPlan SuperMarioSessionAgent::beginDisconnect() noexcept {
    DisconnectPlan plan;
    plan.closeSocket = true;

    if (closing_) {
        return plan;
    }

    closing_ = true;
    if (playerId_ > 0) {
        plan.sendLeaveToWorld = true;
        plan.playerId = playerId_;
    } else {
        plan.eraseSession = true;
    }
    return plan;
}

ResponsePlan SuperMarioSessionAgent::onWorldResponse(const GameResponse& response) noexcept {
    ResponsePlan plan;
    if (response.type == GameResponseType::Joined) {
        plan.outboundText = acceptJoin(response.playerId, response.text);
        return plan;
    }

    plan.outboundText = response.text;
    if (response.type == GameResponseType::LeaveAck) {
        plan.eraseSession = true;
        plan.closeSocket = response.closeAfterSend;
        closing_ = response.closeAfterSend;
        playerId_ = 0;
        return plan;
    }

    if (response.closeAfterSend) {
        closing_ = true;
        plan.closeSocket = true;
    }
    return plan;
}

GameCommand SuperMarioSessionAgent::makeJoinCommand() const {
    return GameCommand{GameCommandType::Join, fd_, 0, {}};
}

GameCommand SuperMarioSessionAgent::makeLeaveCommand() const {
    return GameCommand{GameCommandType::Leave, fd_, playerId_, {}};
}

std::string SuperMarioSessionAgent::ensureNewline(std::string data) {
    if (data.empty() || data.back() != '\n') {
        data.push_back('\n');
    }
    return data;
}

std::string SuperMarioSessionAgent::trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

} // namespace supermario
