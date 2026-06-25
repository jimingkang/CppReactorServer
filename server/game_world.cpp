#include "game_world.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

int GameWorld::join() {
    const int id = nextPlayerId_++;
    Player player;
    player.id = id;
    players_.emplace(id, player);
    return id;
}

std::string GameWorld::leave(int playerId) {
    players_.erase(playerId);
    return "BYE player=" + std::to_string(playerId) + "\n";
}

Player* GameWorld::find(int playerId) {
    auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

std::string GameWorld::handleCommand(int playerId, const std::string& commandLine) {
    Player* player = find(playerId);
    if (player == nullptr) {
        return "ERR player_not_found\n";
    }

    std::istringstream input(commandLine);
    std::string command;
    input >> command;

    if (command == "MOVE") {
        int dx = 0;
        int dy = 0;
        input >> dx >> dy;
        player->x = std::clamp(player->x + dx, -100, 100);
        player->y = std::clamp(player->y + dy, -100, 100);
        player->score += std::max(1, std::abs(dx) + std::abs(dy));
        return "OK MOVE player=" + std::to_string(playerId) + " x=" + std::to_string(player->x) +
               " y=" + std::to_string(player->y) + " score=" + std::to_string(player->score) + "\n";
    }

    if (command == "POS") {
        int x = 0;
        int y = 0;
        input >> x >> y;
        player->x = std::clamp(x, 0, 2400);
        player->y = std::clamp(y, 0, 720);
        player->score += 1;
        return "OK POS player=" + std::to_string(playerId) + " x=" + std::to_string(player->x) +
               " y=" + std::to_string(player->y) + " score=" + std::to_string(player->score) + "\n";
    }

    if (command == "ATTACK") {
        int targetId = 0;
        input >> targetId;
        Player* target = find(targetId);
        if (target == nullptr) {
            return "ERR target_not_found\n";
        }
        target->hp = std::max(0, target->hp - 10);
        player->score += 5;
        return "OK ATTACK target=" + std::to_string(targetId) + " hp=" + std::to_string(target->hp) + "\n";
    }

    if (command == "STATE") {
        return snapshot();
    }

    if (command == "PING") {
        return "PONG\n";
    }

    if (command == "HELP") {
        return "COMMANDS POS x y | MOVE dx dy | ATTACK playerId | STATE | PING | QUIT\n";
    }

    if (command == "QUIT") {
        return "QUIT\n";
    }

    return "ERR unknown_command. Try HELP\n";
}

std::string GameWorld::snapshot() const {
    std::ostringstream out;
    out << "STATE players=" << players_.size() << "\n";
    for (const auto& [id, player] : players_) {
        out << "PLAYER id=" << id << " x=" << player.x << " y=" << player.y
            << " hp=" << player.hp << " score=" << player.score << "\n";
    }
    return out.str();
}
