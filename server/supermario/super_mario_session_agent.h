#pragma once

#include "../i_session_agent.h"

#include <string>
#include <string_view>
#include <vector>

namespace supermario {

class SuperMarioSessionAgent final : public ISessionAgent {
public:
    explicit SuperMarioSessionAgent(int fd);

    int fd() const noexcept override;
    int playerId() const noexcept override;
    bool closing() const noexcept override;

    SessionActions onAccept() override;
    SessionActions onSocketData(std::string_view chunk) override;
    SessionActions onDisconnect() noexcept override;
    SessionActions onWorldResponse(const GameResponse& response) noexcept override;
    SessionActions onLoginResponse(const LoginMessage& response) noexcept override;

private:
    static std::string trimLine(std::string line);
    static std::vector<std::string> splitWords(std::string_view line);
    SkynetMessage makeJoinMessage() const;
    SkynetMessage makeLeaveMessage() const;
    SkynetMessage makeLoginRequest(std::string username, std::string password) const;

    int fd_ = -1;
    int playerId_ = 0;
    bool closing_ = false;
    bool authenticated_ = false;
    bool loginPending_ = false;
    std::string username_;
    std::string input_;
};

} // namespace supermario
