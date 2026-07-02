#pragma once

#include <string>
#include <vector>

namespace wowclient {

struct RealmSummary {
    std::string name = "LocalDev";
    std::string address = "127.0.0.1";
    std::string population = "low";
    int characters = 0;
};

struct CharacterSummary {
    std::string name = "ShellAdventurer";
    std::string race = "Human";
    std::string klass = "Warrior";
    std::string gender = "Nonbinary";
    int level = 1;
    std::string zone = "Northshire";
};

struct MapSnapshot {
    int id = 0;
    std::string name = "Northshire";
    std::string zone = "Northshire";
};

struct UnitSnapshot {
    int playerId = 0;
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
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    bool hostile = false;
    bool dead = false;
    bool selected = false;
    bool self = false;
};

struct PositionSnapshot {
    int playerId = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
};

struct TargetSnapshot {
    int playerId = 0;
    int targetId = 0;
    bool hostile = false;
    std::string name = "None";
};

struct CombatSnapshot {
    int playerId = 0;
    int targetId = 0;
    bool autoAttacking = false;
    bool dead = false;
};

struct ItemSnapshot {
    int id = 0;
    std::string name;
    std::string type = "Misc";
    std::string slot = "None";
    int bagSlot = -1;
    bool equipped = false;
    int quality = 1;
    int stack = 1;
    int minDamage = 0;
    int maxDamage = 0;
    float speed = 0.0f;
};

struct InventorySnapshot {
    int slots = 16;
    int equipped = 2;
    int gold = 0;
    std::vector<std::string> items;
    std::vector<ItemSnapshot> detailedItems;
};

struct ChatMessage {
    std::string channel = "SYSTEM";
    std::string from = "World";
    std::string text;
};

struct ActionButton {
    int slot = 0;
    std::string label;
    std::string command;
};

struct QuestSummary {
    int id = 0;
    std::string title;
    std::string status = "available";
    int progress = 0;
    int required = 1;
};

struct SpellSummary {
    int id = 0;
    std::string name;
    int cost = 0;
    int minDamage = 0;
    int maxDamage = 0;
    int cooldownMs = 0;
    int remainingMs = 0;
};

struct VendorItem {
    int id = 0;
    std::string name;
    int price = 0;
    std::string type = "Consumable";
};

struct GossipOption {
    int index = 0;
    std::string text;
};

struct WhoPlayerSummary {
    int id = 0;
    std::string name;
    std::string klass;
    int level = 0;
    std::string zone;
};

} // namespace wowclient
