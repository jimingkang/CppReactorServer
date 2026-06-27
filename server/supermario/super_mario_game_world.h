#pragma once

#include "../platform_game_world.h"
#include "super_mario_types.h"
#include "ecs_poc.h"

#include <mutex>
#include <queue>


#include <string>
#include <unordered_map>
#include <vector>

namespace supermario {

class SuperMarioGameWorld final : public PlatformGameWorld {
public:
    int join() override;
    std::string leave(int playerId) override;
    std::string handleCommand(int playerId, const std::string& commandLine) override;
    std::string snapshot() const override;

    // Advance authoritative simulation by ms milliseconds (called from server tick thread).
    void tick(int ms) override;

private:
    struct PlayerInput {
        // If setPos is true, this is a position update (px,py). Otherwise use dx,dy as delta move.
        bool setPos = false;
        int px = 0;
        int py = 0;
        int dx = 0;
        int dy = 0;
    };

    // Inputs queued by handleCommand and consumed by tick().
    mutable std::mutex inputMutex_;
    std::queue<std::pair<int, PlayerInput>> inputQueue_;

    static std::string commandName(const std::string& commandLine);
    Player* find(int playerId);

    int nextPlayerId_ = 1;
    std::unordered_map<int, Player> players_;
    std::vector<WorldCoin> coins_ = {
        {0, 315, 305}, {1, 370, 305}, {2, 600, 265}, {3, 665, 265},
        {4, 910, 325}, {5, 1240, 280}, {6, 1310, 280}, {7, 1585, 320},
        {8, 1660, 320}, {9, 1905, 270}, {10, 1985, 270}, {11, 2230, 392},
    };
    std::vector<WorldMonster> monsters_ = {
        {0, 720, 402, 690, 820, 70},
        {1, 1060, 402, 1040, 1160, -65},
        {2, 1740, 327, 1730, 1840, 55},
        {3, 2100, 402, 2040, 2200, -80},
    };

    // Minimal server-side ECS for monsters (POC). Keeps original monsters_ for reference but
    // migrates to ecs_ on first snapshot/update. Changes confined to server/supermario.
    EnemyECS ecs_;

    std::unordered_map<int, Player> savedPlayers_;
};

} // namespace supermario
