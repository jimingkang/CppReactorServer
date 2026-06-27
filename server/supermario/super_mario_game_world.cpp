#include "super_mario_game_world.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace supermario {

namespace {

std::string firstToken(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    return command;
}

} // namespace

int SuperMarioGameWorld::join() {
    const int playerId = nextPlayerId_++;
    Player player;
    player.id = playerId;
    players_.emplace(playerId, player);
    return playerId;
}

std::string SuperMarioGameWorld::leave(int playerId) {
    if (auto it = players_.find(playerId); it != players_.end()) {
        savedPlayers_[playerId] = it->second;
        players_.erase(it);
    }
    return "BYE player=" + std::to_string(playerId) + "\n";
}

std::string SuperMarioGameWorld::handleCommand(int playerId, const std::string& commandLine) {
    const std::string command = commandName(commandLine);

    if (command == "PING") {
        return "PONG\n";
    }

    if (command == "HELP") {
        return "COMMANDS POS x y | MOVE dx dy | ATTACK playerId | COIN coinId | STATE | PING | QUIT\n";
    }

    if (command == "QUIT") {
        return "QUIT\n";
    }

    if (command == "STATE") {
        return snapshot();
    }

    Player* player = find(playerId);
    if (player == nullptr) {
        return "ERR player_not_found\n";
    }

    std::istringstream input(commandLine);
    input >> std::ws;
    std::string ignored;
    input >> ignored;

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
        return "OK POS player=" + std::to_string(playerId) + " x=" + std::to_string(player->x) +
               " y=" + std::to_string(player->y) + " score=" + std::to_string(player->score) + "\n";
    }

    if (command == "COIN") {
        int coinId = -1;
        input >> coinId;
        auto it = std::find_if(coins_.begin(), coins_.end(), [coinId](const WorldCoin& coin) {
            return coin.id == coinId;
        });
        if (it == coins_.end()) {
            return "ERR coin_not_found\n";
        }
        if (it->collected) {
            return "ERR coin_already_collected\n";
        }
        it->collected = true;
        player->score += 10;
        return "OK COIN id=" + std::to_string(coinId) + " score=" + std::to_string(player->score) + "\n";
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

    return "ERR unknown_command. Try HELP\n";
}

std::string SuperMarioGameWorld::snapshot() const {
    // Note: const_cast used to allow lazy ECS init/update in this POC. In a full migration
    // the world would own a non-const update loop and snapshots would not mutate state.
    auto* self = const_cast<SuperMarioGameWorld*>(this);

    // Initialize ECS from legacy monsters on first use.
    if (self->ecs_.empty()) {
        self->ecs_.initFrom(self->monsters_);
    }

    // For POC the authoritative update moved to world.tick(). Here snapshot is read-only
    // and will not advance simulation; just export current ECS monster state.
    const auto curMonsters = self->ecs_.snapshotMonsters();

    std::ostringstream out;
    out << "STATE players=" << players_.size() << "\n";
    for (const auto& [id, player] : players_) {
        out << "PLAYER id=" << id << " x=" << player.x << " y=" << player.y
            << " hp=" << player.hp << " score=" << player.score << "\n";
    }
    out << "SAVED_PLAYERS count=" << savedPlayers_.size() << "\n";
    for (const auto& [id, player] : savedPlayers_) {
        out << "SAVED_PLAYER id=" << id << " x=" << player.x << " y=" << player.y
            << " hp=" << player.hp << " score=" << player.score << "\n";
    }
    out << "COINS count=" << coins_.size() << "\n";
    for (const WorldCoin& coin : coins_) {
        out << "COIN id=" << coin.id << " x=" << coin.x << " y=" << coin.y
            << " collected=" << (coin.collected ? 1 : 0) << "\n";
    }
    out << "MONSTERS count=" << curMonsters.size() << "\n";
    for (const WorldMonster& monster : curMonsters) {
        out << "MONSTER id=" << monster.id << " x=" << monster.x << " y=" << monster.y
            << " min=" << monster.minX << " max=" << monster.maxX << " speed=" << monster.speed << "\n";
    }
    return out.str();
}

std::string SuperMarioGameWorld::commandName(const std::string& commandLine) {
    return firstToken(commandLine);
}

Player* SuperMarioGameWorld::find(int playerId) {
    auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

void SuperMarioGameWorld::tick(int ms) {
    // Initialize ECS if needed.
    if (ecs_.empty()) {
        ecs_.initFrom(monsters_);
    }
    // For POC define server tick unit as 100ms -> one ECS tick per 100ms.
    const int ticks = std::max(1, ms / 100);
    ecs_.update(ticks);
}

} // namespace supermario
