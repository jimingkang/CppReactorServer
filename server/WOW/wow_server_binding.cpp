#include "wow_server_binding.h"

#include "wow_service_contexts.h"
#include "wow_session_agent.h"

namespace wow {

namespace {

class NullWowGameWorld final : public IGameWorld {
public:
    GameResponse join(const GameCommand& command) override {
        GameResponse response;
        response.fd = command.fd;
        response.playerId = command.playerId;
        response.roomId = command.roomId;
        response.text = "ERR world_service_disabled\n";
        return response;
    }

    GameResponse leave(const GameCommand& command) override {
        GameResponse response;
        response.type = GameResponseType::LeaveAck;
        response.fd = command.fd;
        response.playerId = command.playerId;
        response.roomId = command.roomId;
        return response;
    }

    GameResponse handleCommand(const GameCommand& command) override {
        GameResponse response;
        response.fd = command.fd;
        response.playerId = command.playerId;
        response.roomId = command.roomId;
        response.text = "ERR unknown_command\n";
        return response;
    }

    std::string snapshot() const override { return {}; }
};

} // namespace

std::unique_ptr<IGameWorld> WowServerBinding::createWorld() {
    return std::make_unique<NullWowGameWorld>();
}

std::unique_ptr<ISessionAgent> WowServerBinding::createSessionAgent(int fd) {
    return std::make_unique<WowSessionAgent>(fd);
}

std::vector<UserCredential> WowServerBinding::seedUsers() {
    return {
        {"gm", "gm"},
        {"player", "player"},
    };
}

std::vector<std::unique_ptr<ServiceContext>> WowServerBinding::createExtraServices() {
    std::vector<std::unique_ptr<ServiceContext>> services;
    services.push_back(std::make_unique<CharacterServiceContext>());
    services.push_back(std::make_unique<MapInstanceServiceContext>());
    services.push_back(std::make_unique<CombatServiceContext>());
    return services;
}

std::vector<SkynetMessage> WowServerBinding::createTickMessages(int ms) {
    std::vector<SkynetMessage> messages;
    SkynetMessage mapTick;
    mapTick.destination = ServiceId::WowMapInstance;
    mapTick.kind = MessageKind::WowRuntime;
    mapTick.wowRuntime.op = WowRuntimeOp::Tick;
    mapTick.wowRuntime.value0 = ms;
    messages.push_back(std::move(mapTick));

    SkynetMessage combatTick;
    combatTick.destination = ServiceId::WowCombat;
    combatTick.kind = MessageKind::WowRuntime;
    combatTick.wowRuntime.op = WowRuntimeOp::Tick;
    combatTick.wowRuntime.value0 = ms;
    messages.push_back(std::move(combatTick));
    return messages;
}

} // namespace wow
