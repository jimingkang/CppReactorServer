#include "session_service_context.h"

#include "worker_server.h"

#include <utility>

SessionServiceContext::SessionServiceContext(std::unique_ptr<ISessionAgent> agent)
    : RequestReplyServiceContext(ServiceId::Connection), agent_(std::move(agent)) {}

SessionServiceContext::~SessionServiceContext() = default;

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

bool SessionServiceContext::shouldStop() const noexcept {
    return retired_;
}

bool SessionServiceContext::isLoginCallMessage(const SkynetMessage& message) {
    return message.destination == ServiceId::Login &&
           message.kind == MessageKind::Login &&
           message.login.type == LoginMessageType::Request;
}

bool SessionServiceContext::isWorldCallMessage(const SkynetMessage& message) {
    if (message.destination != ServiceId::GameWorld || message.kind != MessageKind::GameCommand) {
        return false;
    }

    const GameCommand& command = message.gameCommand;
    if (command.type == GameCommandType::Join || command.type == GameCommandType::Leave) {
        return true;
    }
    return command.type == GameCommandType::Command && command.line == "QUIT";
}

SessionActions SessionServiceContext::withoutSessionCall(SessionActions actions, std::optional<SkynetMessage>& callMessage, SessionCallKind& callKind) {
    std::vector<SkynetMessage> passthrough;
    passthrough.reserve(actions.serviceMessages.size());
    for (auto& serviceMessage : actions.serviceMessages) {
        if (!callMessage.has_value() && isLoginCallMessage(serviceMessage)) {
            callKind = SessionCallKind::Login;
            callMessage = std::move(serviceMessage);
            continue;
        }
        if (!callMessage.has_value() && isWorldCallMessage(serviceMessage)) {
            callKind = SessionCallKind::World;
            callMessage = std::move(serviceMessage);
            continue;
        }
        passthrough.push_back(std::move(serviceMessage));
    }
    actions.serviceMessages = std::move(passthrough);
    return actions;
}

auto SessionServiceContext::mainLoop() -> Task {
    while (!retired_) {
        SkynetMessage message = co_await nextMessage();
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
            co_await processSessionActions(std::move(actions));
            continue;
        }

        if (message.kind == MessageKind::GameResponse) {
            actions = agent_->onWorldResponse(message.gameResponse);
            co_await processSessionActions(std::move(actions));
            continue;
        }

        if (message.kind == MessageKind::Login) {
            actions = agent_->onLoginResponse(message.login);
            co_await processSessionActions(std::move(actions));
            continue;
        }
    }
}

auto SessionServiceContext::processSessionActions(SessionActions actions) -> Task {
    while (!retired_) {
        std::optional<SkynetMessage> callMessage;
        SessionCallKind callKind = SessionCallKind::None;
        SessionActions immediate = withoutSessionCall(std::move(actions), callMessage, callKind);
        processActions(immediate);
        if (retired_ || !callMessage.has_value()) {
            co_return;
        }

        SkynetMessage response = co_await callService(std::move(*callMessage));
        switch (callKind) {
        case SessionCallKind::Login:
            actions = agent_->onLoginResponse(response.login);
            break;
        case SessionCallKind::World:
            actions = agent_->onWorldResponse(response.gameResponse);
            break;
        case SessionCallKind::None:
            co_return;
        }
    }
}

void SessionServiceContext::processActions(const SessionActions& actions) {
    server().applySessionActions(actions);
    if (actions.eraseSession) {
        retired_ = true;
    }
}
