#pragma once

#include "game/World/World.h"
#include "../game_world.h"

#include <array>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

namespace wow {

class WowGameWorld final : public GameWorld {
public:
    WowGameWorld();

    GameResponse join(const GameCommand& command) override;
    GameResponse leave(const GameCommand& command) override;
    GameResponse handleCommand(const GameCommand& command) override;
    std::string snapshot() const override;
    std::vector<GameResponse> takePendingResponses() override;
    void tick(int ms) override;

private:
    struct SpellState {
        int id = 0;
        std::string name;
        int powerCost = 0;
        int minDamage = 0;
        int maxDamage = 0;
        int cooldownMs = 0;
        int remainingCooldownMs = 0;
    };

    struct ItemState {
        int id = 0;
        std::string name;
        std::string type = "Misc";
        std::string equipSlot = "None";
        int quality = 1;
        int stack = 1;
        int minDamage = 0;
        int maxDamage = 0;
        float speed = 0.0f;
        bool equipped = false;
        int bagSlot = -1;
    };

    struct QuestState {
        int id = 0;
        std::string title;
        std::string status = "available";
        int progress = 0;
        int required = 1;
    };

    struct PlayerState {
        int playerId = 0;
        int fd = -1;
        std::string name = "ShellAdventurer";
        std::string race = "Human";
        std::string klass = "Warrior";
        std::string gender = "Nonbinary";
        std::string faction = "Alliance";
        int level = 1;
        int hp = 100;
        int hpMax = 100;
        int power = 100;
        int powerMax = 100;
        int mapId = 0;
        int instanceId = 1;
        std::string zone = "Northshire";
        float x = 48.0f;
        float y = 48.0f;
        float z = 0.0f;
        float o = 0.0f;
        int targetId = 0;
        bool autoAttacking = false;
        bool dead = false;
        bool ghost = false;
        int gold = 25;
        int attackCooldownMs = 0;
        int globalCooldownMs = 0;
        int corpseReleaseMsRemaining = 0;
        float corpseX = 48.0f;
        float corpseY = 48.0f;
        float corpseZ = 0.0f;
        std::vector<ItemState> items;
        std::vector<QuestState> quests;
        std::vector<SpellState> spells;
        std::array<std::string, 13> actionBar{};
    };

    struct WorldUnit {
        int id = 0;
        std::string name;
        std::string race = "Creature";
        std::string klass = "Beast";
        std::string faction = "Neutral";
        std::string zone = "Northshire";
        int level = 1;
        int hp = 100;
        int hpMax = 100;
        int power = 0;
        int powerMax = 0;
        float x = 60.0f;
        float y = 60.0f;
        float z = 0.0f;
        float o = 0.0f;
        bool hostile = false;
        bool dead = false;
        bool vendor = false;
        bool gossip = false;
        bool trainer = false;
        bool lootAvailable = false;
        int lootGold = 0;
        int lootItemId = 0;
        int respawnMsRemaining = 0;
        int attackCooldownMs = 0;
        float spawnX = 60.0f;
        float spawnY = 60.0f;
        float spawnZ = 0.0f;
        float spawnO = 0.0f;
    };

    static std::vector<ItemState> starterItems();
    static std::vector<QuestState> starterQuests();
    static std::vector<SpellState> starterSpells(const std::string& klass);
    static std::array<std::string, 13> starterActionBar();
    PlayerState* findPlayer(int playerId);
    const PlayerState* findPlayer(int playerId) const;
    WorldUnit* findUnit(int unitId);
    const WorldUnit* findUnit(int unitId) const;
    SpellState* findSpell(PlayerState& state, const std::string& name);
    int nextHostileTarget(int currentTarget) const;
    std::string commandHelp() const;
    std::string snapshotFor(int playerId) const;
    std::string movementSnapshot(const PlayerState& state) const;
    std::string targetSnapshot(const PlayerState& state) const;
    std::string combatSnapshot(const PlayerState& state) const;
    std::string inventorySnapshot(const PlayerState& state) const;
    std::string questSnapshot(const PlayerState& state) const;
    std::string spellbookSnapshot(const PlayerState& state) const;
    std::string vendorSnapshot() const;
    std::string gossipSnapshot() const;
    std::string whoSnapshot(const PlayerState& state) const;
    std::string unitLineForPlayer(const PlayerState& state, int selfPlayerId) const;
    std::string unitLineForWorldUnit(const WorldUnit& unit, int targetId) const;
    GameResponse handleMove(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleTarget(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) const;
    GameResponse handleAttack(PlayerState& state, const GameCommand& command);
    GameResponse handleCast(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleEquip(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleUnequip(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleChat(PlayerState& state, const std::string& line, const GameCommand& command);
    GameResponse handleQuestAccept(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleQuestTurnIn(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleLoot(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleVendorBuy(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleGossipSelect(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) const;
    GameResponse handleTeleport(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleRelease(PlayerState& state, const GameCommand& command);
    GameResponse handleTrain(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command);
    GameResponse handleResurrect(PlayerState& state, const GameCommand& command);
    void seedWorldUnits();
    void broadcastToMap(int mapId, int instanceId, int exceptPlayerId, std::string text);
    void applyKillCredit(PlayerState& state, const WorldUnit& target);
    void tickPlayer(PlayerState& state, int ms);
    void tickUnit(WorldUnit& unit, int ms);

    mutable std::mutex mutex_;
    std::unordered_map<int, PlayerState> players_;
    std::unordered_map<int, WorldUnit> worldUnits_;
    std::vector<GameResponse> pendingResponses_;
    World world_;
};

} // namespace wow
