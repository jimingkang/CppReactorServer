#include "ecs_poc.h"

namespace supermario {

void EnemyECS::initFrom(const std::vector<WorldMonster>& monsters) {
    entities_.clear();
    entities_.reserve(monsters.size());
    for (const auto& m : monsters) {
        Entity e;
        e.pos.x = m.x;
        e.pos.y = m.y;
        e.patrol.minX = m.minX;
        e.patrol.maxX = m.maxX;
        e.patrol.speed = m.speed;
        e.patrol.dir = (m.speed >= 0) ? 1 : -1;
        e.patrol.id = m.id;
        entities_.push_back(e);
    }
}

void EnemyECS::update(int ticks) {
    if (entities_.empty()) return;
    for (int t = 0; t < ticks; ++t) {
        for (auto& e : entities_) {
            // move
            e.pos.x += e.patrol.speed * e.patrol.dir;
            // reverse if out of bounds
            if (e.pos.x < e.patrol.minX) {
                e.pos.x = e.patrol.minX;
                e.patrol.dir = 1;
            } else if (e.pos.x > e.patrol.maxX) {
                e.pos.x = e.patrol.maxX;
                e.patrol.dir = -1;
            }
        }
    }
}

std::vector<WorldMonster> EnemyECS::snapshotMonsters() const {
    std::vector<WorldMonster> out;
    out.reserve(entities_.size());
    for (const auto& e : entities_) {
        WorldMonster m;
        m.id = e.patrol.id;
        m.x = e.pos.x;
        m.y = e.pos.y;
        m.minX = e.patrol.minX;
        m.maxX = e.patrol.maxX;
        m.speed = e.patrol.speed * e.patrol.dir; // reflect current direction in speed sign
        out.push_back(m);
    }
    return out;
}

} // namespace supermario
