#pragma once

#include "super_mario_types.h"
#include <vector>

namespace supermario {

struct Position {
    int x = 0;
    int y = 0;
};

struct Patrol {
    int minX = 0;
    int maxX = 0;
    int speed = 0; // pixels per tick
    int dir = 1;   // 1 = moving right, -1 = left
    int id = 0;
};

// Minimal, header-only-friendly ECS for enemy patrol POC
class EnemyECS {
public:
    EnemyECS() = default;

    void initFrom(const std::vector<WorldMonster>& monsters);
    void update(int ticks = 1);
    std::vector<WorldMonster> snapshotMonsters() const;

    bool empty() const { return entities_.empty(); }

private:
    struct Entity {
        Position pos;
        Patrol patrol;
    };

    std::vector<Entity> entities_;
};

} // namespace supermario
