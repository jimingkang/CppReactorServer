#pragma once

#include <string>

class IGameWorld {
public:
    virtual ~IGameWorld() = default;

    virtual int join() = 0;
    virtual std::string leave(int playerId) = 0;
    virtual std::string handleCommand(int playerId, const std::string& commandLine) = 0;
    virtual std::string snapshot() const = 0;
    virtual void tick(int /*ms*/) {}
};
