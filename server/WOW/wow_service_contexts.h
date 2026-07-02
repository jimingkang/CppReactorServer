#pragma once

#include "../request_reply_service_context.h"

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace wow {

class CharacterServiceContext final : public RequestReplyServiceContext {
public:
    CharacterServiceContext();

    struct CharacterRecord {
        std::string name;
        std::string race;
        std::string klass;
        std::string gender;
        int level = 1;
        int mapId = 0;
        int instanceId = 1;
        std::string zone = "Northshire";
    };

protected:
    Task mainLoop() override;

private:
    struct ItemRecord {
        int id = 0;
        std::string name;
        std::string type = "Misc";
        std::string slot = "None";
        int quality = 1;
        int stack = 1;
        int minDamage = 0;
        int maxDamage = 0;
        float speed = 0.0f;
        bool equipped = false;
        int bagSlot = -1;
    };

    struct SpellRecord {
        int id = 0;
        std::string name;
        int powerCost = 0;
        int minDamage = 0;
        int maxDamage = 0;
        int cooldownMs = 0;
        int remainingCooldownMs = 0;
    };

    struct QuestRecord {
        int id = 0;
        std::string title;
        std::string status = "available";
        int progress = 0;
        int required = 1;
    };

    struct VendorRecord {
        int id = 0;
        std::string name;
        int price = 0;
        std::string type = "Consumable";
    };

    struct ActiveProfile {
        int fd = -1;
        int playerId = 0;
        std::string accountName;
        std::string characterName;
        std::string klass = "Warrior";
        std::string race = "Human";
        std::string gender = "Nonbinary";
        int level = 1;
        int mapId = 0;
        int instanceId = 1;
        std::string zone = "Northshire";
        int gold = 25;
        std::vector<ItemRecord> items;
        std::vector<SpellRecord> spells;
        std::vector<QuestRecord> quests;
        std::array<std::string, 13> actionBar{};
    };

    void seedAccount(std::string_view accountName);
    SkynetMessage makeReply(const SkynetMessage& request, WowRuntimeMessage runtime) const;
    std::vector<CharacterRecord>& rosterFor(std::string_view accountName);
    ActiveProfile& ensureActiveProfile(const WowRuntimeMessage& runtime);
    static std::vector<ItemRecord> starterItems();
    static std::vector<SpellRecord> starterSpells(std::string_view klass);
    static std::vector<QuestRecord> starterQuests();
    static std::vector<VendorRecord> vendorItems();
    static std::array<std::string, 13> starterActionBar();
    static std::string spellbookPayload(const ActiveProfile& profile);
    static std::string questPayload(const ActiveProfile& profile);
    static std::string inventoryPayload(const ActiveProfile& profile);
    static std::string vendorPayload();
    static std::string gossipPayload();
    std::string whoPayload() const;

    std::unordered_map<std::string, std::vector<CharacterRecord>> accountCharacters_;
    std::unordered_map<int, ActiveProfile> activeProfiles_;
};

class MapInstanceServiceContext final : public RequestReplyServiceContext {
public:
    MapInstanceServiceContext();

protected:
    Task mainLoop() override;

private:
    struct PlayerLocation {
        int fd = -1;
        int playerId = 0;
        std::string name = "ShellAdventurer";
        int mapId = 0;
        int instanceId = 1;
        float x = 48.0f;
        float y = 48.0f;
        float z = 0.0f;
        float o = 0.0f;
    };

    struct WorldUnitRecord {
        int id = 0;
        std::string name;
        std::string race = "Creature";
        std::string klass = "Beast";
        std::string faction = "Neutral";
        int level = 1;
        int hp = 100;
        int hpMax = 100;
        int power = 0;
        int powerMax = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        bool hostile = false;
        bool dead = false;
        bool lootAvailable = false;
        int lootGold = 0;
        int respawnMsRemaining = 0;
        float spawnX = 0.0f;
        float spawnY = 0.0f;
        float spawnZ = 0.0f;
        float spawnO = 0.0f;
    };

    struct MapInstanceState {
        int mapId = 0;
        int instanceId = 1;
        std::vector<int> playerIds;
        std::vector<WorldUnitRecord> units;
    };

    static std::string instanceKey(int mapId, int instanceId);
    static std::vector<WorldUnitRecord> unitsForMap(int mapId);
    static std::string snapshotPayload(const PlayerLocation& location, int playerCount);
    static std::string unitPayloadForMap(int mapId);
    static std::string unitLine(const WorldUnitRecord& unit);
    static std::string sanitizeToken(std::string value);
    static WorldUnitRecord* findUnit(MapInstanceState& instance, int unitId);
    static const WorldUnitRecord* findUnit(const MapInstanceState& instance, int unitId);
    SkynetMessage makeReply(const SkynetMessage& request, WowRuntimeMessage runtime) const;

    std::unordered_map<int, PlayerLocation> players_;
    std::unordered_map<std::string, MapInstanceState> instances_;
};

class CombatServiceContext final : public RequestReplyServiceContext {
public:
    CombatServiceContext();

protected:
    Task mainLoop() override;

private:
    struct CombatActorState {
        int playerId = 0;
        int targetId = 0;
        bool autoAttacking = false;
        bool dead = false;
        bool ghost = false;
        int hp = 100;
        int hpMax = 100;
        int power = 100;
        int powerMax = 100;
    };

    SkynetMessage makeReply(const SkynetMessage& request, WowRuntimeMessage runtime) const;

    std::unordered_map<int, CombatActorState> actors_;
};

} // namespace wow
