#include "ecs_poc.h"
#include <cmath>
#include <algorithm>

namespace supermario {

// Monster systems (old EnemyECS logic)
void GameECS::initMonstersFrom(const std::vector<WorldMonster>& monsters) {
    monsterEntities_.clear();
    monsterEntities_.reserve(monsters.size());
    for (const auto& m : monsters) {
        MonsterEntity e;
        e.pos.x = m.x;
        e.pos.y = m.y;
        e.patrol.minX = m.minX;
        e.patrol.maxX = m.maxX;
        e.patrol.speed = m.speed;
        e.patrol.dir = (m.speed >= 0) ? 1 : -1;
        e.patrol.id = m.id;
        monsterEntities_.push_back(e);
    }
}

void GameECS::updateMonsters(int ticks) {
    if (monsterEntities_.empty()) return;
    for (int t = 0; t < ticks; ++t) {
        for (auto& e : monsterEntities_) {
            e.pos.x += e.patrol.speed * e.patrol.dir;
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

std::vector<WorldMonster> GameECS::snapshotMonsters() const {
    std::vector<WorldMonster> out;
    out.reserve(monsterEntities_.size());
    for (const auto& e : monsterEntities_) {
        WorldMonster m;
        m.id = e.patrol.id;
        m.x = e.pos.x;
        m.y = e.pos.y;
        m.minX = e.patrol.minX;
        m.maxX = e.patrol.maxX;
        m.speed = e.patrol.speed * e.patrol.dir;
        out.push_back(m);
    }
    return out;
}

// Player systems
int GameECS::createPlayer(int playerId) {
    if (playerEntities_.count(playerId)) return playerId;
    PlayerEntity pe;
    pe.id = playerId;
    pe.pos.x = 70;
    pe.pos.y = 260;
    pe.vel.vx = 0;
    pe.vel.vy = 0;
    pe.health.hp = 100;
    pe.health.score = 0;
    playerEntities_[playerId] = pe;
    return playerId;
}

void GameECS::destroyPlayer(int playerId) {
    playerEntities_.erase(playerId);
}

Player* GameECS::getPlayer(int playerId) {
    auto it = playerEntities_.find(playerId);
    if (it == playerEntities_.end()) return nullptr;
    PlayerEntity& pe = it->second;
    // Return a proxy pointer (POC: assumes Player layout matches)
    return reinterpret_cast<Player*>(&pe);
}

const Player* GameECS::getPlayer(int playerId) const {
    auto it = playerEntities_.find(playerId);
    if (it == playerEntities_.end()) return nullptr;
    const PlayerEntity& pe = it->second;
    return reinterpret_cast<const Player*>(&pe);
}

std::vector<Player> GameECS::snapshotPlayers() const {
    std::vector<Player> out;
    out.reserve(playerEntities_.size());
    for (const auto& [pid, pe] : playerEntities_) {
        Player p;
        p.id = pe.id;
        p.x = pe.pos.x;
        p.y = pe.pos.y;
        p.hp = pe.health.hp;
        p.score = pe.health.score;
        out.push_back(p);
    }
    return out;
}

void GameECS::updatePhysics(int ms) {
    // Monster movement
    const int ticks = std::max(1, ms / 100);
    updateMonsters(ticks);

    // Player physics: apply velocity & gravity
    const double dt = static_cast<double>(ms) / 1000.0;
    constexpr int gravity = 1450;  // pixels/sec^2
    constexpr int maxY = 720;
    constexpr int boundX = 2400;

    for (auto& [pid, pe] : playerEntities_) {
        // Apply gravity (simple model, no jumping logic here)
        pe.vel.vy += static_cast<int>(gravity * dt);

        // Integrate velocity
        const int moveX = static_cast<int>(std::round(pe.vel.vx * dt));
        const int moveY = static_cast<int>(std::round(pe.vel.vy * dt));
        pe.pos.x = std::clamp(pe.pos.x + moveX, 0, boundX);
        pe.pos.y += moveY;

        // Simple ground detection: if y >= some platform height, stop falling
        // (POC: use a fixed ground level; real game would check against platform geometry)
        constexpr int groundY = 680;
        if (pe.pos.y >= groundY) {
            pe.pos.y = groundY;
            pe.vel.vy = 0;
        }

        // Boundary check: if fallen off map, respawn
        if (pe.pos.y > maxY) {
            pe.pos.y = groundY;
            pe.vel.vy = 0;
        }
    }
}

void GameECS::checkCollisions(std::vector<WorldCoin>& coins) {
    const auto curMonsters = snapshotMonsters();

    for (auto& [pid, pe] : playerEntities_) {
        // Coin collisions
        for (auto& coin : coins) {
            if (coin.collected) continue;
            const int dx = pe.pos.x - coin.x;
            const int dy = pe.pos.y - coin.y;
            const int dist2 = dx * dx + dy * dy;
            if (dist2 <= 24 * 24) {
                coin.collected = true;
                pe.health.score += 10;
            }
        }

        // Monster collisions
        for (const auto& m : curMonsters) {
            const int mdx = pe.pos.x - m.x;
            const int mdy = pe.pos.y - m.y;
            if (std::abs(mdx) < 32 && std::abs(mdy) < 28) {
                // Simple damage (no stomping logic for now)
                pe.health.hp = std::max(0, pe.health.hp - 10);
            }
        }
    }
}

} // namespace supermario
