#include "guess_number_session_agent.h"

#include <cctype>

namespace guessnumber {

namespace {

SkynetMessage wrapHall(HallMessage hall) {
    SkynetMessage message;
    message.destination = ServiceId::Hall;
    message.kind = MessageKind::Hall;
    message.hall = std::move(hall);
    return message;
}

SkynetMessage wrapWorld(GameCommand command) {
    SkynetMessage message;
    message.destination = ServiceId::GameWorld;
    message.kind = MessageKind::GameCommand;
    message.gameCommand = std::move(command);
    return message;
}

} // namespace

GuessNumberSessionAgent::GuessNumberSessionAgent(int fd)
    : fd_(fd), playerId_(fd) {}

int GuessNumberSessionAgent::fd() const noexcept {
    return fd_;
}

int GuessNumberSessionAgent::playerId() const noexcept {
    return playerId_;
}

bool GuessNumberSessionAgent::closing() const noexcept {
    return closing_;
}

SessionActions GuessNumberSessionAgent::onAccept() {
    SessionActions actions;
    actions.socketCommands.push_back(SocketCommand{
        SocketCommandType::Send,
        fd_,
        "HALL status=matching seats=3 game=guess-number\n"
    });
    waitingMatch_ = true;
    actions.serviceMessages.push_back(makeAutoMatchMessage());
    return actions;
}

SessionActions GuessNumberSessionAgent::onSocketData(std::string_view chunk) {
    input_.append(chunk.data(), chunk.size());
    SessionActions actions;

    std::size_t pos = 0;
    while ((pos = input_.find('\n')) != std::string::npos) {
        std::string line = trimLine(input_.substr(0, pos + 1));
        input_.erase(0, pos + 1);
        if (line.empty()) {
            continue;
        }

        const std::string command = upperFirst(line);
        if (command == "PING") {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, "PONG\n"});
            continue;
        }
        if (command == "HELP") {
            actions.socketCommands.push_back(SocketCommand{
                SocketCommandType::Send,
                fd_,
                "COMMANDS GUESS number | STATE | PING | HELP | QUIT\n"
            });
            continue;
        }
        if (command == "QUIT") {
            quitPending_ = true;
            if (waitingMatch_) {
                actions.serviceMessages.push_back(makeCancelMatchMessage());
            } else if (joinedRoom_) {
                actions.serviceMessages.push_back(makeLeaveWorldMessage());
            } else {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, "BYE\n"});
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
                actions.eraseSession = true;
                closing_ = true;
            }
            continue;
        }
        if (waitingMatch_) {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, "WAITING match\n"});
            continue;
        }
        if (!joinedRoom_) {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, "ERR room_not_joined\n"});
            continue;
        }
        actions.serviceMessages.push_back(makeWorldCommand(std::move(line)));
    }

    return actions;
}

SessionActions GuessNumberSessionAgent::onDisconnect() noexcept {
    SessionActions actions;
    closing_ = true;
    if (joinedRoom_) {
        actions.serviceMessages.push_back(makeLeaveWorldMessage());
        return actions;
    }
    if (waitingMatch_) {
        quitPending_ = true;
        actions.serviceMessages.push_back(makeCancelMatchMessage());
        return actions;
    }
    actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
    actions.eraseSession = true;
    return actions;
}

SessionActions GuessNumberSessionAgent::onWorldResponse(const GameResponse& response) noexcept {
    SessionActions actions;
    if (response.type == GameResponseType::Joined) {
        playerId_ = response.playerId;
        roomId_ = response.roomId;
        joinedRoom_ = true;
        waitingMatch_ = false;
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            "ROOM_JOINED room=" + std::to_string(roomId_) +
            " player=" + std::to_string(playerId_) + "\n" + response.text
        });
        return actions;
    }

    if (!response.text.empty()) {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, response.text});
    }

    if (response.type == GameResponseType::LeaveAck || response.closeAfterSend) {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
        actions.eraseSession = true;
        closing_ = true;
        joinedRoom_ = false;
        waitingMatch_ = false;
    }
    return actions;
}

SessionActions GuessNumberSessionAgent::onLoginResponse(const LoginMessage& response) noexcept {
    (void)response;
    return {};
}

SessionActions GuessNumberSessionAgent::onHallResponse(const HallMessage& response) noexcept {
    SessionActions actions;
    waitingMatch_ = false;

    if (quitPending_ && response.type == HallMessageType::Result) {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, fd_, "BYE\n"});
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, fd_, {}});
        actions.eraseSession = true;
        closing_ = true;
        return actions;
    }

    if (!response.success) {
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            "ERR match_failed reason=" + response.reason + "\n"
        });
        return actions;
    }

    roomId_ = response.roomId;
    actions.socketCommands.push_back(SocketCommand{
        SocketCommandType::Send,
        fd_,
        "MATCHED room=" + std::to_string(roomId_) + " players=" + std::to_string(response.playerIds.size()) + "\n"
    });
    actions.serviceMessages.push_back(makeJoinWorldMessage());
    return actions;
}

SessionActions GuessNumberSessionAgent::onReconnect(int newFd) {
    SessionActions actions;
    fd_ = newFd;
    
    if (joinedRoom_) {
        // 重新加入房间
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            "RECONNECTED room=" + std::to_string(roomId_) +
            " player=" + std::to_string(playerId_) + "\n"
        });
        actions.serviceMessages.push_back(makeJoinWorldMessage());
    } else if (waitingMatch_) {
        // 继续等待匹配
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            "WAITING match_resumed\n"
        });
    } else {
        // 开始新的匹配
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            fd_,
            "RECONNECTED new_session\n"
        });
        waitingMatch_ = true;
        actions.serviceMessages.push_back(makeAutoMatchMessage());
    }
    
    return actions;
}

std::string GuessNumberSessionAgent::trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

std::string GuessNumberSessionAgent::upperFirst(std::string line) {
    for (char& ch : line) {
        if (std::isspace(static_cast<unsigned char>(ch)) != 0) {
            break;
        }
        ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
    }
    std::size_t end = line.find(' ');
    for (std::size_t i = 0; i < (end == std::string::npos ? line.size() : end); ++i) {
        line[i] = static_cast<char>(std::toupper(static_cast<unsigned char>(line[i])));
    }
    return line.substr(0, end == std::string::npos ? line.size() : end);
}

SkynetMessage GuessNumberSessionAgent::makeAutoMatchMessage() const {
    HallMessage hall;
    hall.type = HallMessageType::AutoMatch;
    hall.fd = fd_;
    hall.playerId = playerId_;
    hall.seatCount = 3;
    hall.gameType = "guess-number";
    return wrapHall(std::move(hall));
}

SkynetMessage GuessNumberSessionAgent::makeCancelMatchMessage() const {
    HallMessage hall;
    hall.type = HallMessageType::CancelMatch;
    hall.fd = fd_;
    hall.playerId = playerId_;
    hall.gameType = "guess-number";
    hall.seatCount = 3;
    return wrapHall(std::move(hall));
}

SkynetMessage GuessNumberSessionAgent::makeJoinWorldMessage() const {
    return wrapWorld(GameCommand{GameCommandType::Join, fd_, playerId_, roomId_, {}});
}

SkynetMessage GuessNumberSessionAgent::makeLeaveWorldMessage() const {
    return wrapWorld(GameCommand{GameCommandType::Leave, fd_, playerId_, roomId_, {}});
}

SkynetMessage GuessNumberSessionAgent::makeWorldCommand(std::string line) const {
    return wrapWorld(GameCommand{GameCommandType::Command, fd_, playerId_, roomId_, std::move(line)});
}

} // namespace guessnumber
