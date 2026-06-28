#pragma once

#include "../i_game_server_binding.h"

namespace supermario {

class SuperMarioServerBinding final : public IGameServerBinding {
public:
    std::unique_ptr<IGameWorld> createWorld() override;
    std::unique_ptr<ISessionAgent> createSessionAgent(int fd) override;
    std::vector<UserCredential> seedUsers() override;
};

} // namespace supermario
