#pragma once

#include "../i_session_agent.h"

#include <string>
#include <string_view>

namespace guessnumber {

class GuessNumberSessionAgent final : public ISessionAgent {
public:
    explicit GuessNumberSessionAgent(int fd);

    int fd() const noexcept override;
    int playerId() const noexcept override;
    bool closing() const noexcept override;

    SessionActions onAccept() override;
    SessionActions onSocketData(std::string_view chunk) override;
    SessionActions onDisconnect() noexcept override;
    SessionActions onWorldResponse(const GameResponse& response) noexcept override;
    SessionActions onLoginResponse(const LoginMessage& response) noexcept override;
    SessionActions onHallResponse(const HallMessage& response) noexcept override;
    
    // 用户恢复（重连时调用）
    SessionActions onReconnect(int newFd);

private:
    static std::string trimLine(std::string line);
    static std::string upperFirst(std::string line);
    SkynetMessage makeAutoMatchMessage() const;
    SkynetMessage makeCancelMatchMessage() const;
    SkynetMessage makeJoinWorldMessage() const;
    SkynetMessage makeLeaveWorldMessage() const;
    SkynetMessage makeWorldCommand(std::string line) const;

    int fd_ = -1;
    int playerId_ = 0;
    int roomId_ = 0;
    bool closing_ = false;
    bool waitingMatch_ = false;
    bool joinedRoom_ = false;
    bool quitPending_ = false;
    std::string input_;
};

} // namespace guessnumber
