#pragma once

#include "wow_types.h"

#include <map>
#include <string>
#include <vector>

namespace wowclient {

class WorldSession {
public:
    ~WorldSession();

    bool connectNow(const std::string& host, int port);
    void disconnect();
    void poll();

    void sendAuth(const std::string& username, const std::string& password);
    void requestRealmList();
    void requestCharacterList();
    void createCharacter(const std::string& name, const std::string& race, const std::string& klass, const std::string& gender);
    void enterWorld(const std::string& characterName);
    void requestState();
    void ping();
    void help();
    void quit();
    void sendCommand(const std::string& command);
    void move(const std::string& direction, float amount = 2.0f);
    void target(int targetId);
    void tabTarget();
    void attack();
    void cast(const std::string& spell);
    void equip(int itemId);
    void unequip(const std::string& slot);
    void say(const std::string& text);
    void requestQuests();
    void requestSpellbook();
    void requestWho();
    void acceptQuest(int questId);
    void turnInQuest(int questId);
    void requestVendor();
    void buyItem(int itemId);
    void requestGossip();
    void gossipSelect(int index);
    void loot(int targetId);
    void teleport(int mapId);
    void train(const std::string& spell);
    void release();
    void resurrect();

    bool connected() const noexcept;
    bool authenticated() const noexcept;
    bool enteringWorld() const noexcept;
    bool worldEntered() const noexcept;
    int selfPlayerId() const noexcept;
    int playerCount() const noexcept;
    int instanceId() const noexcept;
    int queuedCommandCount() const noexcept;
    const std::string& worldName() const noexcept;
    const std::string& motd() const noexcept;
    const std::string& status() const noexcept;
    const std::string& authFailure() const noexcept;
    const std::string& accountName() const noexcept;
    const RealmSummary& selectedRealm() const noexcept;
    const std::vector<RealmSummary>& realms() const noexcept;
    const std::vector<CharacterSummary>& characters() const noexcept;
    const CharacterSummary& activeCharacter() const noexcept;
    const MapSnapshot& map() const noexcept;
    const UnitSnapshot& selfUnit() const noexcept;
    const std::vector<UnitSnapshot>& nearbyUnits() const noexcept;
    const PositionSnapshot& position() const noexcept;
    const TargetSnapshot& target() const noexcept;
    const CombatSnapshot& combat() const noexcept;
    const InventorySnapshot& inventory() const noexcept;
    const std::vector<ChatMessage>& chatLog() const noexcept;
    const std::vector<ActionButton>& actionBar() const noexcept;
    const std::vector<QuestSummary>& quests() const noexcept;
    const std::vector<SpellSummary>& spells() const noexcept;
    const std::vector<VendorItem>& vendorItems() const noexcept;
    const std::vector<GossipOption>& gossipOptions() const noexcept;
    const std::vector<WhoPlayerSummary>& whoPlayers() const noexcept;
    const std::vector<std::string>& rawLog() const noexcept;

private:
    void sendLine(const std::string& line);
    void consumeLine(const std::string& line);

    int fd_ = -1;
    bool connected_ = false;
    bool authenticated_ = false;
    bool enteringWorld_ = false;
    bool worldEntered_ = false;
    std::string partial_;
    std::string writeBuffer_;
    std::vector<std::string> rawLog_;
    std::string worldName_ = "Azeroth";
    std::string motd_ = "Connect to the world server.";
    std::string status_ = "disconnected";
    std::string authFailure_;
    std::string accountName_ = "player";
    int selfPlayerId_ = 0;
    int playerCount_ = 0;
    int instanceId_ = 0;
    int queuedCommandCount_ = 0;
    RealmSummary selectedRealm_;
    std::vector<RealmSummary> realms_;
    std::vector<CharacterSummary> characters_;
    CharacterSummary activeCharacter_;
    MapSnapshot map_;
    UnitSnapshot selfUnit_;
    std::vector<UnitSnapshot> nearbyUnits_;
    PositionSnapshot position_;
    TargetSnapshot target_;
    CombatSnapshot combat_;
    InventorySnapshot inventory_;
    std::vector<ChatMessage> chatLog_;
    std::vector<ActionButton> actionBar_;
    std::vector<QuestSummary> quests_;
    std::vector<SpellSummary> spells_;
    std::vector<VendorItem> vendorItems_;
    std::vector<GossipOption> gossipOptions_;
    std::vector<WhoPlayerSummary> whoPlayers_;
};

} // namespace wowclient
