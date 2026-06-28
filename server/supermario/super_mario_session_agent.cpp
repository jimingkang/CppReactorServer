#include "super_mario_session_agent.h"

namespace supermario {

namespace {

SkynetMessage wrapGameMessage(ServiceId destination, GameCommand command) {
    SkynetMessage message;
    message.destination = destination;
    message.kind = MessageKind::GameCommand;
    message.gameCommand = std::move(command);
    return message;
}

SkynetMessage wrapLoginMessage(ServiceId destination, LoginMessage login) {
    SkynetMessage message;
    message.destination = destination;
    message.kind = MessageKind::Login;
    message.login = std::move(login);
    return message;
}

} // namespace

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

SessionActions SuperMarioSessionAgent::onAccept() {
    SessionActions actions;
    actions.socketCommands.push_back(SocketCommand{
        SocketCommandType::Send,
        fd_,
        "LOGIN required. Use: LOGIN <user> <pass>\n"
    });
    return actions;
}

SessionActions SuperMarioSessionAgent::onSocketData(std::string_view chunk) {
    input_.append(chunk.data(), chunk.size());

    SessionActions actions;
    std::size_t pos = 0;
    while ((pos = input_.find('\n')) != std::string::npos) {
        std::string line = trimLine(input_.substr(0, pos + 1));
        input_.erase(0, pos + 1);
        if (line.empty()) {
            continue;
        }

        const auto words = splitWords(line);
        const std::string command = words.empty() ? std::string{} : words.front();

        if (!authenticated_) {
            if (command == "PING") {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, "PONG\n"});
                continue;
            }
            if (command == "HELP") {
                actions.socketCommands.push_back(SocketCommand{
                    SocketCommandType::Send,
                    fd_,
                    "COMMANDS LOGIN user pass | PING | HELP | QUIT\n"
                });
                continue;
            }
            if (command == "QUIT") {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
                actions.eraseSession = true;
                closing_ = true;
                continue;
            }
            if (command != "LOGIN") {
                actions.socketCommands.push_back(SocketCommand{
                    SocketCommandType::Send,
                    fd_,
                    "ERR login_required\n"
                });
                continue;
            }
            if (loginPending_) {
                actions.socketCommands.push_back(SocketCommand{
                    SocketCommandType::Send,
                    fd_,
                    "ERR login_pending\n"
                });
                continue;
            }
            if (words.size() < 3) {
                actions.socketCommands.push_back(SocketCommand{
                    SocketCommandType::Send,
                    fd_,
                    "ERR usage LOGIN user pass\n"
                });
                continue;
            }
            loginPending_ = true;
            actions.serviceMessages.push_back(makeLoginRequest(words[1], words[2]));
            continue;
        }

        if (!line.empty()) {
            actions.serviceMessages.push_back(wrapGameMessage(
                ServiceId::GameWorld,
                GameCommand{GameCommandType::Command, fd_, playerId_, std::move(line)}));
        }
    }
    return actions;
}

static std::string buildJoinPayload(int playerId, std::string snapshot) {
    std::string payload = "WELCOME player=" + std::to_string(playerId) + "\n";
    payload += "COMMANDS INPUT seq vx vy | MOVE dx dy | POS x y | ATTACK playerId | COIN coinId | STATE | PING | QUIT\n";
    payload += std::move(snapshot);
    return payload;
}

SessionActions SuperMarioSessionAgent::onDisconnect() noexcept {
    SessionActions actions;
    actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});

    if (closing_) {
        return actions;
    }

    closing_ = true;
    if (playerId_ > 0) {
        actions.serviceMessages.push_back(makeLeaveMessage());
    } else {
        actions.eraseSession = true;
    }
    return actions;
}

SessionActions SuperMarioSessionAgent::onWorldResponse(const GameResponse& response) noexcept {
    SessionActions actions;
    if (response.type == GameResponseType::Joined) {
        playerId_ = response.playerId;
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            buildJoinPayload(response.playerId, response.text)
        });
        return actions;
    }

    if (!response.text.empty()) {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, response.text});
    }

    if (response.type == GameResponseType::LeaveAck) {
        actions.eraseSession = true;
        if (response.closeAfterSend) {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
        }
        closing_ = response.closeAfterSend;
        playerId_ = 0;
        return actions;
    }

    if (response.closeAfterSend) {
        closing_ = true;
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
    }
    return actions;
}

SessionActions SuperMarioSessionAgent::onLoginResponse(const LoginMessage& response) noexcept {
    SessionActions actions;
    loginPending_ = false;
    if (!response.success) {
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            "ERR login_failed reason=" + response.reason + "\n"
        });
        return actions;
    }

    authenticated_ = true;
    username_ = response.username;
    actions.socketCommands.push_back(SocketCommand{
        SocketCommandType::Send,
        fd_,
        "OK LOGIN user=" + username_ + "\n"
    });
    actions.serviceMessages.push_back(makeJoinMessage());
    return actions;
}

SkynetMessage SuperMarioSessionAgent::makeJoinMessage() const {
    return wrapGameMessage(ServiceId::GameWorld, GameCommand{GameCommandType::Join, fd_, 0, {}});
}

SkynetMessage SuperMarioSessionAgent::makeLeaveMessage() const {
    return wrapGameMessage(ServiceId::GameWorld, GameCommand{GameCommandType::Leave, fd_, playerId_, {}});
}

SkynetMessage SuperMarioSessionAgent::makeLoginRequest(std::string username, std::string password) const {
    LoginMessage login;
    login.type = LoginMessageType::Request;
    login.fd = fd_;
    login.username = std::move(username);
    login.password = std::move(password);
    return wrapLoginMessage(ServiceId::Login, std::move(login));
}

std::string SuperMarioSessionAgent::trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

std::vector<std::string> SuperMarioSessionAgent::splitWords(std::string_view line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i])) != 0) {
            ++i;
        }
        if (i >= line.size()) {
            break;
        }
        std::size_t j = i;
        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])) == 0) {
            ++j;
        }
        out.emplace_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

} // namespace supermario
