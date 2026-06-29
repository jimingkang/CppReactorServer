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
    : rng_(std::random_device{}()) {}

GameResponse GuessNumberGameWorld::join(const GameCommand& command) {
    const int playerId = command.playerId > 0 ? command.playerId : command.fd;
    PlayerState& state = players_[playerId];
    state.playerId = playerId;
    state.fd = command.fd;
    state.roomId = command.roomId;

    RoomState& roomState = rooms_[command.roomId];
    if (roomState.roomId == 0) {
        roomState.roomId = command.roomId;
        roomState.secret = std::uniform_int_distribution<int>(1, 100)(rng_);
    }
    if (std::find(roomState.players.begin(), roomState.players.end(), playerId) == roomState.players.end()) {
        roomState.players.push_back(playerId);
    }

    GameResponse response;
    response.type = GameResponseType::Joined;
    response.fd = command.fd;
    response.playerId = playerId;
    response.roomId = command.roomId;
    response.text = roomSnapshot(command.roomId);

    broadcastRoom(command.roomId,
                  "ROOM_EVENT room=" + std::to_string(command.roomId) +
                  " joined_player=" + std::to_string(playerId) + "\n" +
                  roomSnapshot(command.roomId));
    return response;
}

GameResponse GuessNumberGameWorld::leave(const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::LeaveAck;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = command.roomId;
    response.text = "BYE room=" + std::to_string(command.roomId) + " player=" + std::to_string(command.playerId) + "\n";
    response.closeAfterSend = true;

    auto playerIt = players_.find(command.playerId);
    if (playerIt == players_.end()) {
        return response;
    }

    const int roomId = playerIt->second.roomId;
    auto roomIt = rooms_.find(roomId);
    if (roomIt != rooms_.end()) {
        auto& players = roomIt->second.players;
        players.erase(std::remove(players.begin(), players.end(), command.playerId), players.end());
        if (players.empty()) {
            rooms_.erase(roomIt);
        } else {
            broadcastRoom(roomId,
                          "ROOM_EVENT room=" + std::to_string(roomId) +
                          " left_player=" + std::to_string(command.playerId) + "\n" +
                          roomSnapshot(roomId));
        }
    }

    players_.erase(playerIt);
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

    PlayerState* playerState = player(command.playerId);
    if (playerState == nullptr) {
        response.text = "ERR player_not_found\n";
        return response;
    }
    RoomState* roomState = room(playerState->roomId);
    if (roomState == nullptr) {
        response.text = "ERR room_not_found\n";
        return response;
    }

    if (op == "STATE") {
        response.text = roomSnapshot(roomState->roomId);
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

    if (roomState->finished) {
        response.text = "GAME_ALREADY_OVER room=" + std::to_string(roomState->roomId) +
                        " loser=" + std::to_string(roomState->losingPlayerId) +
                        " secret=" + std::to_string(roomState->secret) + "\n" +
                        roomSnapshot(roomState->roomId);
        return response;
    }

    std::string hint;
    if (guess < roomState->secret) {
        hint = "higher";
    } else if (guess > roomState->secret) {
        hint = "lower";
    } else {
        hint = "hit";
    }
    roomState->history.push_back("player=" + std::to_string(command.playerId) +
                                 " guess=" + std::to_string(guess) +
                                 " result=" + hint);

    if (hint != "hit") {
        broadcastRoom(roomState->roomId,
                      "GUESS room=" + std::to_string(roomState->roomId) +
                      " player=" + std::to_string(command.playerId) +
                      " value=" + std::to_string(guess) +
                      " hint=" + hint + "\n" +
                      roomSnapshot(roomState->roomId));
        response.text.clear();
        return response;
    }

    roomState->finished = true;
    roomState->losingPlayerId = command.playerId;
    for (int pid : roomState->players) {
        auto it = players_.find(pid);
        if (it == players_.end()) {
            continue;
        }
        if (pid == command.playerId) {
            it->second.score -= 3;
        } else {
            it->second.score += 3;
        }
    }

    broadcastRoom(roomState->roomId,
                  "GAMEOVER room=" + std::to_string(roomState->roomId) +
                  " loser=" + std::to_string(command.playerId) +
                  " secret=" + std::to_string(roomState->secret) + "\n" +
                  roomSnapshot(roomState->roomId));
    response.text.clear();
    return response;
}

std::string GuessNumberGameWorld::snapshot() const {
    std::ostringstream out;
    out << "ROOMS count=" << rooms_.size() << "\n";
    for (const auto& [roomId, roomState] : rooms_) {
        out << roomSnapshot(roomId);
    }
    return out.str();
}

std::vector<GameResponse> GuessNumberGameWorld::takePendingResponses() {
    std::vector<GameResponse> out;
    out.swap(pendingResponses_);
    return out;
}

GuessNumberGameWorld::PlayerState* GuessNumberGameWorld::player(int playerId) {
    const auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

GuessNumberGameWorld::RoomState* GuessNumberGameWorld::room(int roomId) {
    const auto it = rooms_.find(roomId);
    return it == rooms_.end() ? nullptr : &it->second;
}

std::string GuessNumberGameWorld::roomSnapshot(int roomId) const {
    const auto it = rooms_.find(roomId);
    if (it == rooms_.end()) {
        return "ROOM room=" + std::to_string(roomId) + " missing=1\n";
    }

    const RoomState& roomState = it->second;
    std::ostringstream out;
    out << "ROOM room=" << roomState.roomId
        << " players=" << roomState.players.size()
        << " finished=" << (roomState.finished ? 1 : 0) << "\n";
    for (int pid : roomState.players) {
        const auto playerIt = players_.find(pid);
        if (playerIt == players_.end()) {
            continue;
        }
        out << "PLAYER id=" << playerIt->second.playerId
            << " score=" << playerIt->second.score
            << " room=" << playerIt->second.roomId << "\n";
    }
    if (!roomState.history.empty()) {
        out << "LAST " << roomState.history.back() << "\n";
    }
    return out.str();
}

void GuessNumberGameWorld::broadcastRoom(int roomId, std::string text) {
    const auto it = rooms_.find(roomId);
    if (it == rooms_.end()) {
        return;
    }
    for (int pid : it->second.players) {
        const auto playerIt = players_.find(pid);
        if (playerIt == players_.end()) {
            continue;
        }
        GameResponse response;
        response.type = GameResponseType::Text;
        response.fd = playerIt->second.fd;
        response.playerId = pid;
        response.roomId = roomId;
        response.text = text;
        pendingResponses_.push_back(std::move(response));
    }
}

} // namespace guessnumber
