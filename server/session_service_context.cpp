#include "session_service_context.h"

#include "worker_server.h"

SessionServiceContext::SessionServiceContext(std::unique_ptr<ISessionAgent> agent)
    : ServiceContext(ServiceId::Connection), agent_(std::move(agent)) {}

int SessionServiceContext::fd() const noexcept {
    return agent_->fd();
}

bool SessionServiceContext::closing() const noexcept {
    return agent_->closing();
}

bool SessionServiceContext::retired() const noexcept {
    return retired_;
}

ISessionAgent& SessionServiceContext::agent() noexcept {
    return *agent_;
}

void SessionServiceContext::dispatch(WorkerGameServer& server, const SkynetMessage& message) {
    if (retired_) {
        return;
    }

    SessionActions actions;
    if (message.kind == MessageKind::Socket) {
        switch (message.socket.type) {
        case SocketMessageType::Accept:
            actions = agent_->onAccept();
            break;
        case SocketMessageType::Data:
            actions = agent_->onSocketData(message.socket.data);
            break;
        case SocketMessageType::Close:
        case SocketMessageType::Error:
            actions = agent_->onDisconnect();
            break;
        }
    } else if (message.kind == MessageKind::GameResponse) {
        actions = agent_->onWorldResponse(message.gameResponse);
    } else if (message.kind == MessageKind::Login) {
        actions = agent_->onLoginResponse(message.login);
    } else {
        return;
    }

    server.applySessionActions(actions);
    if (actions.eraseSession) {
        retired_ = true;
    }
}
