#pragma once

#include "worker_protocol.h"

#include <string>
#include <vector>

class IGameWorld {
public:
    virtual ~IGameWorld() = default;

    virtual GameResponse join(const GameCommand& command) = 0;
    virtual GameResponse leave(const GameCommand& command) = 0;
    virtual GameResponse handleCommand(const GameCommand& command) = 0;
    virtual std::string snapshot() const = 0;
    virtual std::vector<GameResponse> takePendingResponses() { return {}; }
    virtual void tick(int /*ms*/) {}
};
