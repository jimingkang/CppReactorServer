#pragma once

#include "game/Server/Protocol/WorldSocket.h"
#include "game/Server/WorldSession.h"
#include "../i_session_agent.h"

#include <vector>
#include <string>
#include <string_view>

namespace wow {

class WowSessionAgent final : public ISessionAgent {
public:
    explicit WowSessionAgent(int fd);

    int fd() const noexcept override;
    int playerId() const noexcept override;
    bool closing() const noexcept override;

    SessionActions onAccept() override;
    SessionActions onSocketData(std::string_view chunk) override;
    SessionActions onDisconnect() noexcept override;
    SessionActions onWorldResponse(const GameResponse& response) noexcept override;
    SessionActions onLoginResponse(const LoginMessage& response) noexcept override;
    SessionActions onHallResponse(const HallMessage& response) noexcept override;
    SessionActions onWowRuntimeResponse(const WowRuntimeMessage& response) noexcept override;

private:
    SessionActions routeInWorldCommand(std::string line) const;
    static std::string trimLine(std::string line);
    static std::vector<std::string> splitWords(std::string_view line);
    static std::string parseField(std::string_view payload, std::string_view key);
    void loadCharactersFromPayload(std::string_view payload);
    std::string realmListMessage() const;
    std::string characterListMessage() const;
    SkynetMessage makeJoinMessage() const;
    SkynetMessage makeLeaveMessage() const;
    SkynetMessage makeWorldCommand(std::string line) const;
    SkynetMessage makeLoginRequest(std::string username, std::string password) const;
    SkynetMessage makeCharacterEnumRequest() const;
    SkynetMessage makeCharacterCreateRequest(std::string characterName, std::string klass) const;
    SkynetMessage makeCharacterLoginRequest() const;
    SkynetMessage makeCharacterWhoRequest() const;
    SkynetMessage makeCharacterSpellbookRequest() const;
    SkynetMessage makeCharacterQuestListRequest() const;
    SkynetMessage makeCharacterQuestAcceptRequest(int questId) const;
    SkynetMessage makeCharacterQuestTurnInRequest(int questId) const;
    SkynetMessage makeCharacterVendorListRequest() const;
    SkynetMessage makeCharacterGossipRequest() const;
    SkynetMessage makeCharacterGossipSelectRequest(int index) const;
    SkynetMessage makeCharacterEquipRequest(int itemId) const;
    SkynetMessage makeCharacterUnequipRequest(std::string slot) const;
    SkynetMessage makeCharacterTrainRequest(std::string spell) const;
    SkynetMessage makeCharacterBuyRequest(int itemId) const;
    SkynetMessage makeMapEnterRequest(int mapId, int instanceId) const;
    SkynetMessage makeMapLeaveRequest() const;
    SkynetMessage makeMapSnapshotRequest() const;
    SkynetMessage makeMapMoveRequest(int dx, int dy) const;
    SkynetMessage makeMapTeleportRequest(int mapId) const;
    SkynetMessage makeMapSayRequest(std::string text) const;
    SkynetMessage makeCombatInitRequest() const;
    SkynetMessage makeCombatTargetRequest(int targetId) const;
    SkynetMessage makeCombatAttackRequest() const;
    SkynetMessage makeCombatCastRequest(std::string spellName) const;
    SkynetMessage makeCombatLootRequest(int targetId) const;
    SkynetMessage makeCombatReleaseRequest() const;
    SkynetMessage makeCombatResurrectRequest() const;

    WorldSocketRouter socketRouter_;
    WorldSession session_;
    std::string input_;
};

} // namespace wow
