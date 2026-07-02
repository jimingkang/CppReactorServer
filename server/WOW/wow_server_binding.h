#pragma once

#include "../i_game_server_binding.h"

namespace wow {

class WowServerBinding final : public IGameServerBinding {
public:
    std::unique_ptr<IGameWorld> createWorld() override;
    std::unique_ptr<ISessionAgent> createSessionAgent(int fd) override;
    std::vector<UserCredential> seedUsers() override;
    std::vector<std::unique_ptr<ServiceContext>> createExtraServices() override;
    std::vector<SkynetMessage> createTickMessages(int ms) override;
};

} // namespace wow
