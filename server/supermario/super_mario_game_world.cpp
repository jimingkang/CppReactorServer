#include "super_mario_game_world.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <cmath>
#include <utility>

namespace supermario {

namespace {

std::string firstToken(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    return command;
}

} // namespace

GameResponse SuperMarioGameWorld::join(const GameCommand& command) {
    const int playerId = nextPlayerId_++;
    ecs_.createPlayer(playerId);
    GameResponse response;
    response.type = GameResponseType::Joined;
    response.fd = command.fd;
    response.playerId = playerId;
    response.roomId = command.roomId;
    response.text = snapshot();
    return response;
}

GameResponse SuperMarioGameWorld::leave(const GameCommand& command) {
    ecs_.destroyPlayer(command.playerId);
    GameResponse response;
    response.type = GameResponseType::LeaveAck;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = command.roomId;
    response.text = "BYE player=" + std::to_string(command.playerId) + "\n";
    response.closeAfterSend = true;
    return response;
}

GameResponse SuperMarioGameWorld::handleCommand(const GameCommand& commandData) {
    const int playerId = commandData.playerId;
    const std::string& commandLine = commandData.line;
    const std::string command = commandName(commandLine);
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = commandData.fd;
    response.playerId = playerId;
    response.roomId = commandData.roomId;

    if (command == "PING") {
        response.text = "PONG\n";
        return response;
    }

    if (command == "HELP") {
        response.text = "COMMANDS POS x y | MOVE dx dy | ATTACK playerId | COIN coinId | STATE | PING | QUIT\n";
        return response;
    }

    if (command == "QUIT") {
        response.type = GameResponseType::LeaveAck;
        response.text = leave(commandData).text;
        response.closeAfterSend = true;
        return response;
    }

    if (command == "STATE") {
        response.text = snapshot();
        return response;
    }

    Player* player = find(playerId);
    if (player == nullptr) {
        response.text = "ERR player_not_found\n";
        return response;
    }

    std::istringstream input(commandLine);
    input >> std::ws;
    std::string ignored;
    input >> ignored;

    if (command == "MOVE") {
        int dx = 0;
        int dy = 0;
        input >> dx >> dy;
        PlayerInput pi;
        pi.setPos = false;
        pi.dx = dx;
        pi.dy = dy;
        {
            std::lock_guard lock(inputMutex_);
            inputQueue_.emplace(playerId, pi);
        }
        response.text = "OK MOVE queued player=" + std::to_string(playerId) + " dx=" + std::to_string(dx) +
                        " dy=" + std::to_string(dy) + "\n";
        return response;
    }

    if (command == "POS") {
        int x = 0;
        int y = 0;
        input >> x >> y;
        PlayerInput pi;
        pi.setPos = true;
        pi.px = x;
        pi.py = y;
        {
            std::lock_guard lock(inputMutex_);
            inputQueue_.emplace(playerId, pi);
        }
        response.text = "OK POS queued player=" + std::to_string(playerId) + " x=" + std::to_string(x) +
                        " y=" + std::to_string(y) + "\n";
        return response;
    }

    if (command == "INPUT") {
        int seq = 0;
        int vx = 0;
        int vy = 0;
        input >> seq >> vx >> vy;
        PlayerInput pi;
        pi.setPos = false;
        pi.dx = vx;
        pi.dy = vy;
        {
            std::lock_guard lock(inputMutex_);
            inputQueue_.emplace(playerId, pi);
        }
        response.text = "OK INPUT queued seq=" + std::to_string(seq) + " vx=" + std::to_string(vx) + " vy=" + std::to_string(vy) + "\n";
        return response;
    }

    if (command == "COIN") {
        int coinId = -1;
        input >> coinId;
        auto it = std::find_if(coins_.begin(), coins_.end(), [coinId](const WorldCoin& coin) {
            return coin.id == coinId;
        });
        if (it == coins_.end()) {
            response.text = "ERR coin_not_found\n";
            return response;
        }
        if (it->collected) {
            response.text = "ERR coin_already_collected\n";
            return response;
        }
        it->collected = true;
        player->score += 10;
        response.text = "OK COIN id=" + std::to_string(coinId) + " score=" + std::to_string(player->score) + "\n";
        return response;
    }

    if (command == "ATTACK") {
        int targetId = 0;
        input >> targetId;
        Player* target = find(targetId);
        if (target == nullptr) {
            response.text = "ERR target_not_found\n";
            return response;
        }
        target->hp = std::max(0, target->hp - 10);
        player->score += 5;
        response.text = "OK ATTACK target=" + std::to_string(targetId) + " hp=" + std::to_string(target->hp) + "\n";
        return response;
    }

    response.text = "ERR unknown_command. Try HELP\n";
    return response;
}

std::string SuperMarioGameWorld::snapshot() const {
    auto* self = const_cast<SuperMarioGameWorld*>(this);

    // Initialize ECS from legacy monsters on first use.
    if (self->ecs_.monstersEmpty()) {
        self->ecs_.initMonstersFrom(self->monsters_);
    }

    const auto curMonsters = self->ecs_.snapshotMonsters();
    const auto curPlayers = self->ecs_.snapshotPlayers();

    std::ostringstream out;
    out << "STATE players=" << curPlayers.size() << "\n";
    for (const auto& player : curPlayers) {
        out << "PLAYER id=" << player.id << " x=" << player.x << " y=" << player.y
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
    return ecs_.getPlayer(playerId);
}

void SuperMarioGameWorld::tick(int ms) {
    // Initialize ECS if needed.
    if (ecs_.monstersEmpty()) {
        ecs_.initMonstersFrom(monsters_);
    }

    // Consume queued player inputs and apply to velocity components
    std::queue<std::pair<int, PlayerInput>> pending;
    {
        std::lock_guard lock(inputMutex_);
        std::swap(pending, inputQueue_);
    }

    while (!pending.empty()) {
        const auto [pid, in] = pending.front();
        pending.pop();

        if (!in.setPos) {
            // Apply velocity input to ECS player entity
            ecs_.setPlayerVelocity(pid, in.dx, in.dy);
        } else {
            // setPos case: set absolute position directly (backward compat)
            ecs_.setPlayerPosition(pid, in.px, in.py);
        }
    }

    // Unified physics & collision update: all players, monsters, interactions
    ecs_.updatePhysics(ms);
    ecs_.checkCollisions(coins_);
}


} // namespace supermario
