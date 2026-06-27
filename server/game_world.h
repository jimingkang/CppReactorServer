#pragma once

#include <string>

class GameWorld {
public:
    virtual ~GameWorld() = default;

    virtual int join() = 0;
    virtual std::string leave(int playerId) = 0;
    virtual std::string handleCommand(int playerId, const std::string& commandLine) = 0;
    virtual std::string snapshot() const = 0;

    // Optional per-frame tick (ms). Default no-op so existing worlds remain compatible.
    virtual void tick(int /*ms*/) {}
};
