#pragma once

#include "i_game_world.h"
#include "i_session_agent.h"

#include <string>
#include <memory>
#include <utility>
#include <vector>

class ServiceContext;
struct SkynetMessage;

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
    virtual std::vector<std::unique_ptr<ServiceContext>> createExtraServices() { return {}; }
    virtual std::vector<SkynetMessage> createTickMessages(int /*ms*/) { return {}; }
};
