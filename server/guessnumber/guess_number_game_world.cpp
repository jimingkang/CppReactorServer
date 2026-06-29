#include "guess_number_game_world.h"

#include <algorithm>
#include <sstream>

namespace guessnumber {

namespace {

std::string firstWord(const std::string& line) {
    std::istringstream in(line);
    std::string token;
    in >> token;
    return token;
}

} // namespace

GuessNumberGameWorld::GuessNumberGameWorld()
    : rng_(std::random_device{}()),
      roomManager_(std::make_unique<GuessNumberRoomManager>()) {}

GuessNumberGameWorld::GuessSecret GuessNumberGameWorld::generateSecret() {
    return GuessSecret{std::uniform_int_distribution<int>(1, 100)(rng_), 0};
}

GameResponse GuessNumberGameWorld::join(const GameCommand& command) {
    const int playerId = command.playerId > 0 ? command.playerId : command.fd;
    const int roomId = command.roomId;

    // Ensure player exists
    PlayerInfo* player = roomManager_->getPlayer(playerId);
    if (player == nullptr) {
        player = roomManager_->createPlayer(playerId, command.fd);
    }

    // Ensure room exists with secret
    RoomInfo* room = roomManager_->getRoom(roomId);
    if (room == nullptr) {
        room = roomManager_->createRoom(roomId, 3);
        roomSecrets_[roomId] = generateSecret();
    }

    // Add player to room
    roomManager_->joinRoom(roomId, playerId);
    room->state = RoomState::Playing;

    GameResponse response;
    response.type = GameResponseType::Joined;
    response.fd = command.fd;
    response.playerId = playerId;
    response.roomId = roomId;
    response.text = getRoomSnapshot(roomId);

    broadcastRoom(roomId,
                  "ROOM_EVENT room=" + std::to_string(roomId) +
                  " joined_player=" + std::to_string(playerId) + "\n" +
                  getRoomSnapshot(roomId));
    return response;
}

GameResponse GuessNumberGameWorld::leave(const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::LeaveAck;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = command.roomId;
    response.text = "BYE room=" + std::to_string(command.roomId) +
                    " player=" + std::to_string(command.playerId) + "\n";
    response.closeAfterSend = true;

    PlayerInfo* player = roomManager_->getPlayer(command.playerId);
    if (player == nullptr) {
        return response;
    }

    const int roomId = player->currentRoomId;
    roomManager_->leaveRoom(roomId, command.playerId);

    if (roomManager_->isRoomEmpty(roomId)) {
        roomManager_->removeRoom(roomId);
        roomSecrets_.erase(roomId);
    } else {
        broadcastRoom(roomId,
                      "ROOM_EVENT room=" + std::to_string(roomId) +
                      " left_player=" + std::to_string(command.playerId) + "\n" +
                      getRoomSnapshot(roomId));
    }

    roomManager_->removePlayer(command.playerId);
    return response;
}

GameResponse GuessNumberGameWorld::handleCommand(const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = command.roomId;

    const std::string op = firstWord(command.line);
    if (op == "PING") {
        response.text = "PONG\n";
        return response;
    }
    if (op == "HELP") {
        response.text = "COMMANDS GUESS number | STATE | PING | HELP | QUIT\n";
        return response;
    }
    if (op == "QUIT") {
        return leave(command);
    }

    PlayerInfo* player = getPlayer(command.playerId);
    if (player == nullptr) {
        response.text = "ERR player_not_found\n";
        return response;
    }
    RoomInfo* room = getRoom(player->currentRoomId);
    if (room == nullptr) {
        response.text = "ERR room_not_found\n";
        return response;
    }

    if (op == "STATE") {
        response.text = getRoomSnapshot(room->roomId);
        return response;
    }

    if (op != "GUESS") {
        response.text = "ERR unknown_command\n";
        return response;
    }

    std::istringstream in(command.line);
    std::string ignored;
    int guess = 0;
    in >> ignored >> guess;
    if (!in || guess < 1 || guess > 100) {
        response.text = "ERR usage GUESS 1..100\n";
        return response;
    }

    if (room->state == RoomState::Finished) {
        response.text = "GAME_ALREADY_OVER room=" + std::to_string(room->roomId) +
                        " loser=" + std::to_string(room->losingPlayerId) +
                        " secret=" + std::to_string(roomSecrets_[room->roomId].value) + "\n" +
                        getRoomSnapshot(room->roomId);
        return response;
    }

    auto it = roomSecrets_.find(room->roomId);
    if (it == roomSecrets_.end()) {
        response.text = "ERR game_state_invalid\n";
        return response;
    }

    GuessSecret& secret = it->second;
    secret.attempts++;
    roomManager_->recordGuess(room->roomId, command.playerId, guess);

    std::string hint;
    if (guess < secret.value) {
        hint = "higher";
    } else if (guess > secret.value) {
        hint = "lower";
    } else {
        hint = "hit";
    }

    if (hint != "hit") {
        broadcastRoom(room->roomId,
                      "GUESS room=" + std::to_string(room->roomId) +
                      " player=" + std::to_string(command.playerId) +
                      " value=" + std::to_string(guess) +
                      " hint=" + hint + "\n" +
                      getRoomSnapshot(room->roomId));
        response.text.clear();
        return response;
    }

    // Player found the secret!
    roomManager_->finishRoom(room->roomId, command.playerId);
    room->state = RoomState::Finished;

    broadcastRoom(room->roomId,
                  "GAMEOVER room=" + std::to_string(room->roomId) +
                  " loser=" + std::to_string(command.playerId) +
                  " secret=" + std::to_string(secret.value) +
                  " attempts=" + std::to_string(secret.attempts) + "\n" +
                  getRoomSnapshot(room->roomId));
    response.text.clear();
    return response;
}

std::string GuessNumberGameWorld::snapshot() const {
    std::ostringstream out;
    out << roomManager_->getGlobalStats();
    auto rooms = const_cast<GuessNumberGameWorld*>(this)->roomManager_->getAllRooms();
    for (const auto* room : rooms) {
        out << getRoomSnapshot(room->roomId);
    }
    return out.str();
}

std::vector<GameResponse> GuessNumberGameWorld::takePendingResponses() {
    std::vector<GameResponse> out;
    out.swap(pendingResponses_);
    return out;
}

PlayerInfo* GuessNumberGameWorld::getPlayer(int playerId) {
    return roomManager_->getPlayer(playerId);
}

RoomInfo* GuessNumberGameWorld::getRoom(int roomId) {
    return roomManager_->getRoom(roomId);
}

std::string GuessNumberGameWorld::getRoomSnapshot(int roomId) const {
    return roomManager_->getRoomSummary(roomId);
}

void GuessNumberGameWorld::broadcastRoom(int roomId, std::string text) {
    const auto* room = roomManager_->getRoom(roomId);
    if (room == nullptr) {
        return;
    }
    for (int pid : room->playerIds) {
        const auto* player = roomManager_->getPlayer(pid);
        if (player == nullptr) {
            continue;
        }
        GameResponse response;
        response.type = GameResponseType::Text;
        response.fd = player->fd;
        response.playerId = pid;
        response.roomId = roomId;
        response.text = text;
        pendingResponses_.push_back(std::move(response));
    }
}

} // namespace guessnumber
