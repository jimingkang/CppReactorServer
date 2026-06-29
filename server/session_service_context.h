#pragma once

#include "i_session_agent.h"
#include "request_reply_service_context.h"

#include <memory>
#include <optional>

class SessionServiceContext final : public RequestReplyServiceContext {
public:
    explicit SessionServiceContext(std::unique_ptr<ISessionAgent> agent);
    ~SessionServiceContext() override;

    int fd() const noexcept;
    bool closing() const noexcept;
    bool retired() const noexcept;
    ISessionAgent& agent() noexcept;

private:
    enum class SessionCallKind {
        None,
        Login,
        Hall,
        World,
    };

    Task mainLoop() override;
    bool shouldStop() const noexcept override;

    Task processSessionActions(SessionActions actions);
    void processActions(const SessionActions& actions);

    static bool isLoginCallMessage(const SkynetMessage& message);
    static bool isHallCallMessage(const SkynetMessage& message);
    static bool isWorldCallMessage(const SkynetMessage& message);
    static SessionActions withoutSessionCall(SessionActions actions, std::optional<SkynetMessage>& callMessage, SessionCallKind& callKind);

    std::unique_ptr<ISessionAgent> agent_;
    bool retired_ = false;
};
