#pragma once

#include "i_game_world.h"
#include "i_session_agent.h"

#include <string>
#include <memory>
#include <utility>
#include <vector>

struct UserCredential {
    std::string username;
    std::string password;
};

class IGameServerBinding {
public:
    virtual ~IGameServerBinding() = default;

    virtual std::unique_ptr<IGameWorld> createWorld() = 0;
    virtual std::unique_ptr<ISessionAgent> createSessionAgent(int fd) = 0;
    virtual std::vector<UserCredential> seedUsers() = 0;
};
