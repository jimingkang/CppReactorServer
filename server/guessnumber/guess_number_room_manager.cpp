#include "guess_number_room_manager.h"

#include <algorithm>
#include <sstream>

namespace guessnumber {

GuessNumberRoomManager::GuessNumberRoomManager() = default;

RoomInfo* GuessNumberRoomManager::createRoom(int roomId, int seatCount) {
    (void)seatCount;
    auto& room = rooms_[roomId];
    room.roomId = roomId;
    room.state = RoomState::Waiting;
    room.createdAt = std::chrono::steady_clock::now();
    return &room;
}

RoomInfo* GuessNumberRoomManager::getRoom(int roomId) {
    auto it = rooms_.find(roomId);
    return it == rooms_.end() ? nullptr : &it->second;
}

const RoomInfo* GuessNumberRoomManager::getRoom(int roomId) const {
    auto it = rooms_.find(roomId);
    return it == rooms_.end() ? nullptr : &it->second;
}

bool GuessNumberRoomManager::removeRoom(int roomId) {
    return rooms_.erase(roomId) > 0;
}

PlayerInfo* GuessNumberRoomManager::createPlayer(int playerId, int fd) {
    auto& player = players_[playerId];
    player.playerId = playerId;
    player.fd = fd;
    return &player;
}

PlayerInfo* GuessNumberRoomManager::getPlayer(int playerId) {
    auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

const PlayerInfo* GuessNumberRoomManager::getPlayer(int playerId) const {
    auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

bool GuessNumberRoomManager::removePlayer(int playerId) {
    return players_.erase(playerId) > 0;
}

bool GuessNumberRoomManager::joinRoom(int roomId, int playerId) {
    auto* room = getRoom(roomId);
    if (room == nullptr) {
        return false;
    }
    if (std::find(room->playerIds.begin(), room->playerIds.end(), playerId) != room->playerIds.end()) {
        return true;  // Already joined
    }
    room->playerIds.push_back(playerId);
    
    auto* player = getPlayer(playerId);
    if (player != nullptr) {
        player->currentRoomId = roomId;
    }
    return true;
}

bool GuessNumberRoomManager::leaveRoom(int roomId, int playerId) {
    auto* room = getRoom(roomId);
    if (room == nullptr) {
        return false;
    }
    auto it = std::find(room->playerIds.begin(), room->playerIds.end(), playerId);
    if (it != room->playerIds.end()) {
        room->playerIds.erase(it);
    }
    
    auto* player = getPlayer(playerId);
    if (player != nullptr && player->currentRoomId == roomId) {
        player->currentRoomId = 0;
    }
    return true;
}

bool GuessNumberRoomManager::isRoomFull(int roomId, int maxSeats) const {
    const auto* room = getRoom(roomId);
    if (room == nullptr) {
        return false;
    }
    return static_cast<int>(room->playerIds.size()) >= maxSeats;
}

bool GuessNumberRoomManager::isRoomEmpty(int roomId) const {
    const auto* room = getRoom(roomId);
    if (room == nullptr) {
        return true;
    }
    return room->playerIds.empty();
}

bool GuessNumberRoomManager::recordGuess(int roomId, int playerId, int guess) {
    auto* room = getRoom(roomId);
    if (room == nullptr) {
        return false;
    }
    room->guessHistory.push_back("player=" + std::to_string(playerId) + " guess=" + std::to_string(guess));
    return true;
}

void GuessNumberRoomManager::finishRoom(int roomId, int losingPlayerId) {
    auto* room = getRoom(roomId);
    if (room == nullptr) {
        return;
    }
    room->state = RoomState::Finished;
    room->losingPlayerId = losingPlayerId;
    room->finishedAt = std::chrono::steady_clock::now();
    
    auto* loser = getPlayer(losingPlayerId);
    if (loser != nullptr) {
        loser->gamesPlayed++;
    }
    
    for (int pid : room->playerIds) {
        if (pid != losingPlayerId) {
            auto* winner = getPlayer(pid);
            if (winner != nullptr) {
                winner->gamesWon++;
                winner->gamesPlayed++;
                winner->score += 3;
            }
        } else {
            auto* l = getPlayer(pid);
            if (l != nullptr) {
                l->score -= 3;
            }
        }
    }
}

std::string GuessNumberRoomManager::getRoomSummary(int roomId) const {
    const auto* room = getRoom(roomId);
    if (room == nullptr) {
        return "ROOM room=" + std::to_string(roomId) + " missing=1\n";
    }
    
    std::ostringstream out;
    out << "ROOM room=" << room->roomId
        << " state=" << (room->state == RoomState::Waiting ? "waiting" : 
                         room->state == RoomState::Playing ? "playing" : "finished")
        << " players=" << room->playerIds.size()
        << " finished=" << (room->state == RoomState::Finished ? 1 : 0) << "\n";
    
    for (int pid : room->playerIds) {
        const auto* player = getPlayer(pid);
        if (player == nullptr) {
            continue;
        }
        out << "PLAYER id=" << player->playerId
            << " score=" << player->score
            << " gamesWon=" << player->gamesWon
            << " gamesPlayed=" << player->gamesPlayed << "\n";
    }
    
    if (!room->guessHistory.empty()) {
        out << "LAST " << room->guessHistory.back() << "\n";
    }
    
    return out.str();
}

std::string GuessNumberRoomManager::getPlayerStats(int playerId) const {
    const auto* player = getPlayer(playerId);
    if (player == nullptr) {
        return "PLAYER player=" + std::to_string(playerId) + " missing=1\n";
    }
    
    std::ostringstream out;
    out << "PLAYER id=" << player->playerId
        << " score=" << player->score
        << " gamesWon=" << player->gamesWon
        << " gamesPlayed=" << player->gamesPlayed
        << " currentRoom=" << player->currentRoomId << "\n";
    return out.str();
}

std::string GuessNumberRoomManager::getGlobalStats() const {
    std::ostringstream out;
    out << "STATS rooms=" << rooms_.size()
        << " players=" << players_.size() << "\n";
    
    int totalGames = 0;
    int totalScore = 0;
    for (const auto& [_, player] : players_) {
        totalGames += player.gamesPlayed;
        totalScore += player.score;
    }
    
    out << "GLOBAL total_games=" << totalGames
        << " total_score=" << totalScore << "\n";
    return out.str();
}

std::vector<RoomInfo*> GuessNumberRoomManager::getAllRooms() {
    std::vector<RoomInfo*> result;
    for (auto& [_, room] : rooms_) {
        result.push_back(&room);
    }
    return result;
}

std::vector<PlayerInfo*> GuessNumberRoomManager::getAllPlayers() {
    std::vector<PlayerInfo*> result;
    for (auto& [_, player] : players_) {
        result.push_back(&player);
    }
    return result;
}

void GuessNumberRoomManager::removeExpiredRooms(int timeoutSeconds) {
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(timeoutSeconds);
    
    std::vector<int> toRemove;
    for (auto& [roomId, room] : rooms_) {
        if (room.state == RoomState::Finished &&
            (now - room.finishedAt) > timeout) {
            toRemove.push_back(roomId);
        }
    }
    
    for (int roomId : toRemove) {
        rooms_.erase(roomId);
    }
}

} // namespace guessnumber
