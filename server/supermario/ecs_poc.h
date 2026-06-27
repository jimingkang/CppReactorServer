#pragma once

#include "super_mario_types.h"
#include <vector>
#include <unordered_map>

namespace supermario {

struct Position {
    int x = 0;
    int y = 0;
};

struct Velocity {
    int vx = 0;  // pixels/sec
    int vy = 0;
};

struct Health {
    int hp = 100;
    int score = 0;
};

struct Patrol {
    int minX = 0;
    int maxX = 0;
    int speed = 0; // pixels per tick
    int dir = 1;   // 1 = moving right, -1 = left
    int id = 0;
};

// Unified ECS for players and monsters
class GameECS {
public:
    GameECS() = default;

    // Monster API (unchanged from old EnemyECS)
    void initMonstersFrom(const std::vector<WorldMonster>& monsters);
    void updateMonsters(int ticks = 1);
    std::vector<WorldMonster> snapshotMonsters() const;
    bool monstersEmpty() const { return monsterEntities_.empty(); }

    // Player API
    int createPlayer(int playerId);
    void destroyPlayer(int playerId);
    bool hasPlayer(int playerId) const { return playerEntities_.count(playerId) > 0; }
    Player* getPlayer(int playerId);
    const Player* getPlayer(int playerId) const;
    std::vector<Player> snapshotPlayers() const;

    // Unified physics & collision update per tick
    void updatePhysics(int ms);
    void checkCollisions(std::vector<WorldCoin>& coins);

private:
    struct MonsterEntity {
        Position pos;
        Patrol patrol;
    };

    struct PlayerEntity {
        Position pos;
        Velocity vel;
        Health health;
        int id = 0;
    };

    std::vector<MonsterEntity> monsterEntities_;
    std::unordered_map<int, PlayerEntity> playerEntities_;
};

} // namespace supermario
