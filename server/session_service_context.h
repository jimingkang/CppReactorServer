#pragma once

#include "i_session_agent.h"
#include "service_context.h"

#include <memory>

class SessionServiceContext final : public ServiceContext {
public:
    explicit SessionServiceContext(std::unique_ptr<ISessionAgent> agent);

    int fd() const noexcept;
    bool closing() const noexcept;
    bool retired() const noexcept;
    ISessionAgent& agent() noexcept;

    void dispatch(WorkerGameServer& server, const SkynetMessage& message) override;

private:
    std::unique_ptr<ISessionAgent> agent_;
    bool retired_ = false;
};
