#pragma once

#include "i_game_world.h"

#include <string>

class GameWorld : public IGameWorld {
public:
    virtual ~GameWorld() = default;

    virtual GameResponse join(const GameCommand& command) = 0;
    virtual GameResponse leave(const GameCommand& command) = 0;
    virtual GameResponse handleCommand(const GameCommand& command) = 0;
    virtual std::string snapshot() const = 0;
    virtual std::vector<GameResponse> takePendingResponses() { return {}; }

    // Optional per-frame tick (ms). Default no-op so existing worlds remain compatible.
    virtual void tick(int /*ms*/) {}
};
