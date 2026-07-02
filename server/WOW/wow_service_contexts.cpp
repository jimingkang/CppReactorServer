#include "wow_service_contexts.h"

#include "../worker_server.h"

#include <algorithm>
#include <sstream>
#include <string_view>

namespace wow {

namespace {

std::string rosterPayload(const std::vector<CharacterServiceContext::CharacterRecord>& roster) {
    std::ostringstream out;
    out << "count=" << roster.size();
    for (const auto& character : roster) {
        out << '\n'
            << "name=" << character.name
            << " race=" << character.race
            << " class=" << character.klass
            << " gender=" << character.gender
            << " level=" << character.level
            << " zone=" << character.zone;
    }
    return out.str();
}

std::string zoneNameForMap(int mapId) {
    switch (mapId) {
    case 1:
        return "Elwynn";
    default:
        return "Northshire";
    }
}

std::string mapNameForMap(int mapId) {
    (void)mapId;
    return "Azeroth";
}

std::string sanitizeToken(std::string value) {
    std::replace(value.begin(), value.end(), ' ', '_');
    return value;
}

} // namespace

CharacterServiceContext::CharacterServiceContext()
    : RequestReplyServiceContext(ServiceId::WowCharacter) {}

auto CharacterServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::WowRuntime) {
            continue;
        }

        const WowRuntimeMessage& runtime = message.wowRuntime;
        if (runtime.op == WowRuntimeOp::CharacterEnumRequest) {
            seedAccount(runtime.accountName);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterEnumResult;
            reply.fd = runtime.fd;
            reply.accountName = runtime.accountName;
            reply.success = true;
            reply.payload = rosterPayload(rosterFor(runtime.accountName));
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterCreateRequest) {
            auto& roster = rosterFor(runtime.accountName);
            roster.push_back(CharacterRecord{
                runtime.characterName.empty() ? "NewHero" : runtime.characterName,
                "Human",
                runtime.payload.empty() ? "Warrior" : runtime.payload,
                "Nonbinary",
                1,
                0,
                1,
                "Northshire"
            });

            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterCreateResult;
            reply.fd = runtime.fd;
            reply.accountName = runtime.accountName;
            reply.characterName = roster.back().name;
            reply.success = true;
            reply.payload = rosterPayload(roster);
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterDeleteRequest) {
            auto& roster = rosterFor(runtime.accountName);
            roster.erase(std::remove_if(roster.begin(), roster.end(), [&](const CharacterRecord& record) {
                return record.name == runtime.characterName;
            }), roster.end());

            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterDeleteResult;
            reply.fd = runtime.fd;
            reply.accountName = runtime.accountName;
            reply.characterName = runtime.characterName;
            reply.success = true;
            reply.payload = rosterPayload(roster);
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterLoginBegin) {
            seedAccount(runtime.accountName);

            const auto& roster = rosterFor(runtime.accountName);
            const auto it = std::find_if(roster.begin(), roster.end(), [&](const CharacterRecord& record) {
                return record.name == runtime.characterName;
            });

            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterLoginResult;
            reply.fd = runtime.fd;
            reply.accountName = runtime.accountName;
            reply.characterName = runtime.characterName;
            reply.success = it != roster.end();
            if (it == roster.end()) {
                reply.reason = "character_not_found";
            } else {
                ActiveProfile& profile = ensureActiveProfile(runtime);
                profile.characterName = it->name;
                profile.race = it->race;
                profile.klass = it->klass;
                profile.gender = it->gender;
                profile.level = it->level;
                profile.mapId = it->mapId;
                profile.instanceId = it->instanceId;
                profile.zone = it->zone;
                profile.spells = starterSpells(profile.klass);
                reply.mapId = it->mapId;
                reply.instanceId = it->instanceId;
                reply.payload = "zone=" + it->zone;
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterWhoRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterWhoResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            reply.payload = whoPayload();
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterSpellbookRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterSpellbookResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            reply.payload = spellbookPayload(ensureActiveProfile(runtime));
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterQuestListRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterQuestListResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            reply.payload = questPayload(ensureActiveProfile(runtime));
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterQuestAcceptRequest) {
            ActiveProfile& profile = ensureActiveProfile(runtime);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterQuestAcceptResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            auto it = std::find_if(profile.quests.begin(), profile.quests.end(), [&](const QuestRecord& quest) {
                return quest.id == runtime.value0;
            });
            if (it == profile.quests.end()) {
                reply.success = false;
                reply.reason = "quest_not_found";
            } else if (it->status != "available") {
                reply.success = false;
                reply.reason = "quest_not_available";
            } else {
                it->status = "progress";
                reply.success = true;
                reply.payload = "QUEST_ACCEPT id=" + std::to_string(it->id) + " title=" + it->title + "\n" +
                                questPayload(profile);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterQuestTurnInRequest) {
            ActiveProfile& profile = ensureActiveProfile(runtime);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterQuestTurnInResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            auto it = std::find_if(profile.quests.begin(), profile.quests.end(), [&](const QuestRecord& quest) {
                return quest.id == runtime.value0;
            });
            if (it == profile.quests.end()) {
                reply.success = false;
                reply.reason = "quest_not_found";
            } else if (it->status != "complete") {
                reply.success = false;
                reply.reason = "quest_not_complete";
            } else {
                it->status = "rewarded";
                profile.gold += 12;
                reply.success = true;
                reply.payload = "QUEST_TURNIN id=" + std::to_string(it->id) + " reward_gold=12\n" +
                                questPayload(profile) + inventoryPayload(profile);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterVendorListRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterVendorListResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            reply.payload = vendorPayload();
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterGossipRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterGossipResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            reply.payload = gossipPayload();
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterGossipSelectRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterGossipSelectResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            if (runtime.value0 < 0 || runtime.value0 > 2) {
                reply.success = false;
                reply.reason = "gossip_option_not_found";
            } else {
                reply.success = true;
                reply.payload = "GOSSIP_RESULT index=" + std::to_string(runtime.value0) +
                                " text=" + std::to_string(runtime.value0 == 0 ? 0 : runtime.value0) + "\n";
                if (runtime.value0 == 0) {
                    reply.payload = "GOSSIP_RESULT index=0 text=Where_is_the_training_ground?\n";
                } else if (runtime.value0 == 1) {
                    reply.payload = "GOSSIP_RESULT index=1 text=Show_me_your_goods.\n";
                } else {
                    reply.payload = "GOSSIP_RESULT index=2 text=I_need_work.\n";
                }
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterEquipRequest) {
            ActiveProfile& profile = ensureActiveProfile(runtime);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterEquipResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            const int itemId = runtime.value0;
            auto it = std::find_if(profile.items.begin(), profile.items.end(), [&](const ItemRecord& item) { return item.id == itemId; });
            if (it == profile.items.end()) {
                reply.success = false;
                reply.reason = "item_not_found";
            } else {
                for (auto& item : profile.items) {
                    if (item.slot == it->slot) {
                        item.equipped = false;
                    }
                }
                it->equipped = true;
                reply.success = true;
                reply.payload = inventoryPayload(profile);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterUnequipRequest) {
            ActiveProfile& profile = ensureActiveProfile(runtime);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterUnequipResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            auto it = std::find_if(profile.items.begin(), profile.items.end(), [&](const ItemRecord& item) {
                return item.slot == runtime.payload && item.equipped;
            });
            if (it == profile.items.end()) {
                reply.success = false;
                reply.reason = "slot_empty";
            } else {
                it->equipped = false;
                reply.success = true;
                reply.payload = inventoryPayload(profile);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterTrainRequest) {
            ActiveProfile& profile = ensureActiveProfile(runtime);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterTrainResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            if (profile.gold < 10) {
                reply.success = false;
                reply.reason = "not_enough_gold";
            } else {
                profile.gold -= 10;
                profile.spells.push_back(SpellRecord{
                    5000 + static_cast<int>(profile.spells.size()) + 1,
                    runtime.payload,
                    12,
                    9,
                    15,
                    1500,
                    0
                });
                reply.success = true;
                reply.characterName = runtime.payload;
                reply.payload = "TRAIN_OK spell=" + runtime.payload + " price=10\n" +
                                spellbookPayload(profile) + inventoryPayload(profile);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::CharacterBuyRequest) {
            ActiveProfile& profile = ensureActiveProfile(runtime);
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::CharacterBuyResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            const auto items = vendorItems();
            const int itemId = runtime.value0;
            const auto it = std::find_if(items.begin(), items.end(), [&](const VendorRecord& item) { return item.id == itemId; });
            if (it == items.end()) {
                reply.success = false;
                reply.reason = "vendor_item_not_found";
            } else if (profile.gold < it->price) {
                reply.success = false;
                reply.reason = "not_enough_gold";
            } else {
                profile.gold -= it->price;
                profile.items.push_back(ItemRecord{it->id, it->name, it->type, "None", 1, 1, 0, 0, 0.0f, false, static_cast<int>(profile.items.size())});
                reply.success = true;
                reply.payload = "BUY_OK item=" + it->name + " price=" + std::to_string(it->price) + "\n" +
                                inventoryPayload(profile);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }
    }
}

void CharacterServiceContext::seedAccount(std::string_view accountName) {
    if (accountCharacters_.contains(std::string(accountName))) {
        return;
    }
    accountCharacters_[std::string(accountName)] = {
        {"player_Warrior", "Human", "Warrior", "Nonbinary", 12, 0, 1, "Northshire"},
        {"TrainingMage", "Human", "Mage", "Female", 8, 1, 1, "Elwynn"},
        {"NorthshireRogue", "Human", "Rogue", "Male", 6, 0, 1, "Northshire"},
    };
}

SkynetMessage CharacterServiceContext::makeReply(const SkynetMessage& request, WowRuntimeMessage runtime) const {
    SkynetMessage reply;
    reply.source = ServiceId::WowCharacter;
    reply.destination = request.source;
    reply.kind = MessageKind::WowRuntime;
    reply.requestId = 0;
    reply.replyTo = request.requestId;
    reply.wowRuntime = std::move(runtime);
    return reply;
}

std::vector<CharacterServiceContext::CharacterRecord>& CharacterServiceContext::rosterFor(std::string_view accountName) {
    seedAccount(accountName);
    return accountCharacters_[std::string(accountName)];
}

auto CharacterServiceContext::ensureActiveProfile(const WowRuntimeMessage& runtime) -> ActiveProfile& {
    ActiveProfile& profile = activeProfiles_[runtime.playerId];
    if (profile.playerId == 0) {
        profile.fd = runtime.fd;
        profile.playerId = runtime.playerId;
        profile.accountName = runtime.accountName;
        profile.characterName = runtime.characterName.empty() ? "player_Warrior" : runtime.characterName;
        profile.items = starterItems();
        profile.spells = starterSpells(profile.klass);
        profile.quests = starterQuests();
        profile.actionBar = starterActionBar();
    }
    if (!runtime.accountName.empty()) profile.accountName = runtime.accountName;
    if (!runtime.characterName.empty()) profile.characterName = runtime.characterName;
    return profile;
}

std::vector<CharacterServiceContext::ItemRecord> CharacterServiceContext::starterItems() {
    return {
        {1001, "RustySword", "Weapon", "MainHand", 1, 1, 3, 7, 2.0f, true, -1},
        {1002, "TravelerTunic", "Armor", "Chest", 1, 1, 0, 0, 0.0f, true, -1},
        {1003, "TrainingBoots", "Armor", "Feet", 1, 1, 0, 0, 0.0f, false, 0},
        {1004, "MinorPotion", "Consumable", "None", 1, 2, 0, 0, 0.0f, false, 1},
    };
}

std::vector<CharacterServiceContext::SpellRecord> CharacterServiceContext::starterSpells(std::string_view klass) {
    if (klass == "Mage") {
        return {{2001, "Fireball", 14, 12, 20, 1500, 0}, {2002, "Frostbolt", 10, 8, 14, 1200, 0}};
    }
    return {{2001, "HeroicStrike", 10, 7, 12, 1000, 0}, {2002, "Fireball", 14, 12, 20, 1500, 0}};
}

std::vector<CharacterServiceContext::QuestRecord> CharacterServiceContext::starterQuests() {
    return {
        {3001, "Wolves_Across_Northshire", "progress", 1, 5},
        {3002, "Report_to_Marshal_McBride", "available", 0, 1},
    };
}

std::vector<CharacterServiceContext::VendorRecord> CharacterServiceContext::vendorItems() {
    return {
        {4001, "LinenBandage", 4, "Consumable"},
        {4002, "RecruitBlade", 12, "Weapon"},
        {4003, "ApprenticeRobe", 9, "Armor"},
    };
}

std::array<std::string, 13> CharacterServiceContext::starterActionBar() {
    return {"ATTACK", "CAST Fireball", "CAST HeroicStrike", "MOVE W", "MOVE S", "MOVE A", "MOVE D",
            "TARGET 9001", "TAB_TARGET", "QUEST_LIST", "SPELLBOOK", "VENDOR_LIST", "WHO"};
}

std::string CharacterServiceContext::spellbookPayload(const ActiveProfile& profile) {
    std::ostringstream out;
    out << "SPELLBOOK count=" << profile.spells.size() << "\n";
    for (const auto& spell : profile.spells) {
        out << "SPELL id=" << spell.id
            << " name=" << spell.name
            << " cost=" << spell.powerCost
            << " minDamage=" << spell.minDamage
            << " maxDamage=" << spell.maxDamage
            << " cooldownMs=" << spell.cooldownMs
            << " remainingMs=" << spell.remainingCooldownMs << "\n";
    }
    return out.str();
}

std::string CharacterServiceContext::questPayload(const ActiveProfile& profile) {
    std::ostringstream out;
    out << "QUEST_LIST count=" << profile.quests.size() << "\n";
    for (const auto& quest : profile.quests) {
        out << "QUEST id=" << quest.id
            << " title=" << quest.title
            << " status=" << quest.status
            << " progress=" << quest.progress
            << " required=" << quest.required << "\n";
    }
    return out.str();
}

std::string CharacterServiceContext::inventoryPayload(const ActiveProfile& profile) {
    std::ostringstream out;
    int equipped = 0;
    for (const auto& item : profile.items) {
        if (item.equipped) ++equipped;
    }
    out << "INVENTORY owner=" << profile.playerId
        << " slots=16 equipped=" << equipped
        << " gold=" << profile.gold
        << " items=";
    bool first = true;
    for (const auto& item : profile.items) {
        if (!item.equipped && item.bagSlot >= 0) {
            if (!first) out << ",";
            out << item.name;
            first = false;
        }
    }
    out << "\n";
    for (const auto& item : profile.items) {
        out << "ITEM id=" << item.id
            << " name=" << item.name
            << " type=" << item.type
            << " slot=" << item.slot
            << " bagSlot=" << item.bagSlot
            << " equipped=" << (item.equipped ? 1 : 0)
            << " quality=" << item.quality
            << " stack=" << item.stack
            << " minDamage=" << item.minDamage
            << " maxDamage=" << item.maxDamage
            << " speed=" << item.speed << "\n";
    }
    for (std::size_t slot = 0; slot < profile.actionBar.size(); ++slot) {
        out << "ACTION slot=" << (slot + 1)
            << " label=" << sanitizeToken(profile.actionBar[slot])
            << " command=" << sanitizeToken(profile.actionBar[slot]) << "\n";
    }
    return out.str();
}

std::string CharacterServiceContext::vendorPayload() {
    std::ostringstream out;
    const auto items = vendorItems();
    out << "VENDOR_LIST npc=Brother_Danil count=" << items.size() << "\n";
    for (const auto& item : items) {
        out << "VENDOR_ITEM id=" << item.id
            << " name=" << item.name
            << " price=" << item.price
            << " type=" << item.type << "\n";
    }
    return out.str();
}

std::string CharacterServiceContext::gossipPayload() {
    return "GOSSIP npc=Marshal_McBride count=3\n"
           "GOSSIP_OPTION index=0 text=Where_is_the_training_ground?\n"
           "GOSSIP_OPTION index=1 text=Show_me_your_goods.\n"
           "GOSSIP_OPTION index=2 text=I_need_work.\n";
}

std::string CharacterServiceContext::whoPayload() const {
    std::ostringstream out;
    out << "WHO count=" << activeProfiles_.size() << " map=0 instance=1\n";
    for (const auto& [_, profile] : activeProfiles_) {
        out << "WHO_PLAYER id=" << profile.playerId
            << " name=" << profile.characterName
            << " class=" << profile.klass
            << " level=" << profile.level
            << " zone=" << profile.zone << "\n";
    }
    return out.str();
}

MapInstanceServiceContext::MapInstanceServiceContext()
    : RequestReplyServiceContext(ServiceId::WowMapInstance) {}

auto MapInstanceServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::WowRuntime) {
            continue;
        }

        const WowRuntimeMessage& runtime = message.wowRuntime;
        if (runtime.op == WowRuntimeOp::MapEnterRequest) {
            PlayerLocation& location = players_[runtime.playerId];
            location.fd = runtime.fd;
            location.playerId = runtime.playerId;
            location.name = runtime.characterName.empty() ? location.name : runtime.characterName;
            location.mapId = runtime.mapId;
            location.instanceId = runtime.instanceId > 0 ? runtime.instanceId : 1;

            MapInstanceState& instance = instances_[instanceKey(location.mapId, location.instanceId)];
            instance.mapId = location.mapId;
            instance.instanceId = location.instanceId;
            if (instance.units.empty()) {
                instance.units = unitsForMap(location.mapId);
            }
            if (std::find(instance.playerIds.begin(), instance.playerIds.end(), location.playerId) == instance.playerIds.end()) {
                instance.playerIds.push_back(location.playerId);
            }

            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapEnterResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.mapId = location.mapId;
            reply.instanceId = location.instanceId;
            reply.success = true;
            reply.payload = snapshotPayload(location, static_cast<int>(instance.playerIds.size()));
            for (const auto& unit : instance.units) {
                reply.payload += unitLine(unit);
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapLeaveRequest) {
            const auto playerIt = players_.find(runtime.playerId);
            if (playerIt != players_.end()) {
                const std::string key = instanceKey(playerIt->second.mapId, playerIt->second.instanceId);
                if (auto instanceIt = instances_.find(key); instanceIt != instances_.end()) {
                    auto& playerIds = instanceIt->second.playerIds;
                    playerIds.erase(std::remove(playerIds.begin(), playerIds.end(), runtime.playerId), playerIds.end());
                    if (playerIds.empty()) {
                        instances_.erase(instanceIt);
                    }
                }
                players_.erase(playerIt);
            }

            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapLeaveResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapSnapshotRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapSnapshotResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = true;
            if (const auto it = players_.find(runtime.playerId); it != players_.end()) {
                const auto& location = it->second;
                reply.mapId = location.mapId;
                reply.instanceId = location.instanceId;
                const auto instanceIt = instances_.find(instanceKey(location.mapId, location.instanceId));
                const int playerCount = instanceIt == instances_.end() ? 0 : static_cast<int>(instanceIt->second.playerIds.size());
                reply.payload = snapshotPayload(location, playerCount);
                if (instanceIt != instances_.end()) {
                    for (const auto& unit : instanceIt->second.units) {
                        reply.payload += unitLine(unit);
                    }
                }
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapMoveRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapMoveResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;

            if (auto it = players_.find(runtime.playerId); it != players_.end()) {
                it->second.x += static_cast<float>(runtime.value0);
                it->second.y += static_cast<float>(runtime.value1);
                reply.success = true;
                reply.mapId = it->second.mapId;
                reply.instanceId = it->second.instanceId;
                const auto instanceIt = instances_.find(instanceKey(it->second.mapId, it->second.instanceId));
                const int playerCount = instanceIt == instances_.end() ? 0 : static_cast<int>(instanceIt->second.playerIds.size());
                reply.payload = snapshotPayload(it->second, playerCount);
                if (instanceIt != instances_.end()) {
                    for (const auto& unit : instanceIt->second.units) {
                        reply.payload += unitLine(unit);
                    }
                }
            } else {
                reply.success = false;
                reply.reason = "map_actor_not_found";
            }

            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapTeleportRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapTeleportResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            if (auto it = players_.find(runtime.playerId); it != players_.end()) {
                const std::string oldKey = instanceKey(it->second.mapId, it->second.instanceId);
                if (auto instanceIt = instances_.find(oldKey); instanceIt != instances_.end()) {
                    auto& playerIds = instanceIt->second.playerIds;
                    playerIds.erase(std::remove(playerIds.begin(), playerIds.end(), runtime.playerId), playerIds.end());
                    if (playerIds.empty()) {
                        instances_.erase(instanceIt);
                    }
                }
                it->second.mapId = runtime.mapId;
                it->second.instanceId = runtime.instanceId > 0 ? runtime.instanceId : 1;
                it->second.x = runtime.mapId == 1 ? 64.0f : 48.0f;
                it->second.y = runtime.mapId == 1 ? 44.0f : 48.0f;
                MapInstanceState& instance = instances_[instanceKey(it->second.mapId, it->second.instanceId)];
                instance.mapId = it->second.mapId;
                instance.instanceId = it->second.instanceId;
                if (std::find(instance.playerIds.begin(), instance.playerIds.end(), runtime.playerId) == instance.playerIds.end()) {
                    instance.playerIds.push_back(runtime.playerId);
                }
                if (instance.units.empty()) {
                    instance.units = unitsForMap(it->second.mapId);
                }
                reply.success = true;
                reply.mapId = it->second.mapId;
                reply.instanceId = it->second.instanceId;
                reply.payload = snapshotPayload(it->second, static_cast<int>(instance.playerIds.size()));
                for (const auto& unit : instance.units) {
                    reply.payload += unitLine(unit);
                }
            } else {
                reply.success = false;
                reply.reason = "map_actor_not_found";
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapSayRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapSayResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            reply.success = !runtime.payload.empty();
            if (!reply.success) {
                reply.reason = "usage_SAY_text";
            } else {
                const auto it = players_.find(runtime.playerId);
                const std::string from = it == players_.end() ? runtime.characterName : it->second.name;
                reply.payload = "CHAT channel=SAY from=" + sanitizeToken(from) + " text=" + sanitizeToken(runtime.payload) + "\n";
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapQueryUnitRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapQueryUnitResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            if (const auto playerIt = players_.find(runtime.playerId); playerIt != players_.end()) {
                if (auto instanceIt = instances_.find(instanceKey(playerIt->second.mapId, playerIt->second.instanceId)); instanceIt != instances_.end()) {
                    if (const WorldUnitRecord* unit = findUnit(instanceIt->second, runtime.targetId)) {
                        reply.success = true;
                        reply.targetId = unit->id;
                        reply.characterName = unit->name;
                        reply.value0 = unit->hostile ? 1 : 0;
                        reply.payload = unitLine(*unit);
                    } else {
                        reply.success = false;
                        reply.reason = "target_not_found";
                    }
                } else {
                    reply.success = false;
                    reply.reason = "instance_not_found";
                }
            } else {
                reply.success = false;
                reply.reason = "map_actor_not_found";
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapDamageUnitRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapDamageUnitResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            if (const auto playerIt = players_.find(runtime.playerId); playerIt != players_.end()) {
                if (auto instanceIt = instances_.find(instanceKey(playerIt->second.mapId, playerIt->second.instanceId)); instanceIt != instances_.end()) {
                    if (WorldUnitRecord* unit = findUnit(instanceIt->second, runtime.targetId)) {
                        reply.targetId = unit->id;
                        if (unit->dead) {
                            reply.success = false;
                            reply.reason = "target_dead";
                        } else {
                            unit->hp = std::max(0, unit->hp - runtime.value0);
                            if (unit->hp == 0) {
                                unit->dead = true;
                                unit->lootAvailable = true;
                                unit->respawnMsRemaining = 5000;
                                reply.value1 = 1;
                            }
                            reply.success = true;
                            reply.payload = unitLine(*unit);
                        }
                    } else {
                        reply.success = false;
                        reply.reason = "target_not_found";
                    }
                } else {
                    reply.success = false;
                    reply.reason = "instance_not_found";
                }
            } else {
                reply.success = false;
                reply.reason = "map_actor_not_found";
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::MapLootUnitRequest) {
            WowRuntimeMessage reply;
            reply.op = WowRuntimeOp::MapLootUnitResult;
            reply.fd = runtime.fd;
            reply.playerId = runtime.playerId;
            if (const auto playerIt = players_.find(runtime.playerId); playerIt != players_.end()) {
                if (auto instanceIt = instances_.find(instanceKey(playerIt->second.mapId, playerIt->second.instanceId)); instanceIt != instances_.end()) {
                    if (WorldUnitRecord* unit = findUnit(instanceIt->second, runtime.targetId)) {
                        reply.targetId = unit->id;
                        if (!unit->dead || !unit->lootAvailable) {
                            reply.success = false;
                            reply.reason = "no_loot_available";
                        } else {
                            unit->lootAvailable = false;
                            reply.success = true;
                            reply.value0 = unit->lootGold;
                            reply.payload = unitLine(*unit);
                        }
                    } else {
                        reply.success = false;
                        reply.reason = "target_not_found";
                    }
                } else {
                    reply.success = false;
                    reply.reason = "instance_not_found";
                }
            } else {
                reply.success = false;
                reply.reason = "map_actor_not_found";
            }
            server().sendToService(message.source, makeReply(message, std::move(reply)));
            continue;
        }

        if (runtime.op == WowRuntimeOp::Tick) {
            const int ms = runtime.value0 > 0 ? runtime.value0 : 100;
            for (auto& [_, instance] : instances_) {
                for (auto& unit : instance.units) {
                    if (!unit.dead || unit.respawnMsRemaining <= 0) {
                        continue;
                    }
                    unit.respawnMsRemaining = std::max(0, unit.respawnMsRemaining - ms);
                    if (unit.respawnMsRemaining == 0) {
                        unit.dead = false;
                        unit.lootAvailable = false;
                        unit.hp = unit.hpMax;
                        unit.x = unit.spawnX;
                        unit.y = unit.spawnY;
                        unit.z = unit.spawnZ;
                        unit.o = unit.spawnO;
                    }
                }
            }
        }
    }
}

std::string MapInstanceServiceContext::instanceKey(int mapId, int instanceId) {
    return std::to_string(mapId) + ":" + std::to_string(instanceId);
}

std::vector<MapInstanceServiceContext::WorldUnitRecord> MapInstanceServiceContext::unitsForMap(int mapId) {
    if (mapId == 1) {
        return {
            {9201, "Elwynn_Bandit", "Human", "Rogue", "Hostile", 7, 88, 88, 0, 0, 68.0f, 46.0f, 0.0f, 0.0f, true, false, false, 0, 0, 68.0f, 46.0f, 0.0f, 0.0f},
            {9202, "Elwynn_Guard", "Human", "Warrior", "Alliance", 10, 100, 100, 0, 0, 62.0f, 42.0f, 0.0f, 0.0f, false, false, false, 0, 0, 62.0f, 42.0f, 0.0f, 0.0f},
        };
    }
    return {
        {9001, "Northshire_Wolf", "Wolf", "Beast", "Hostile", 4, 72, 72, 0, 0, 60.0f, 52.0f, 0.0f, 0.0f, true, false, false, 4, 0, 60.0f, 52.0f, 0.0f, 0.0f},
        {9002, "Riverpaw_Scout", "Gnoll", "Rogue", "Hostile", 6, 84, 84, 0, 0, 66.0f, 48.0f, 0.0f, 0.0f, true, false, false, 6, 0, 66.0f, 48.0f, 0.0f, 0.0f},
        {9100, "Marshal_McBride", "Human", "NPC", "Alliance", 10, 100, 100, 0, 0, 43.0f, 47.0f, 0.0f, 0.0f, false, false, false, 0, 0, 43.0f, 47.0f, 0.0f, 0.0f},
        {9101, "Brother_Danil", "Human", "Vendor", "Alliance", 8, 100, 100, 0, 0, 46.0f, 42.0f, 0.0f, 0.0f, false, false, false, 0, 0, 46.0f, 42.0f, 0.0f, 0.0f},
    };
}

std::string MapInstanceServiceContext::snapshotPayload(const PlayerLocation& location, int playerCount) {
    std::ostringstream out;
    out << "WORLD name=wow-shell players=" << playerCount << "\n"
        << "MAP id=" << location.mapId
        << " name=" << mapNameForMap(location.mapId)
        << " zone=" << zoneNameForMap(location.mapId) << "\n"
        << "MAP_INSTANCE map=" << location.mapId
        << " instance=" << location.instanceId
        << " name=" << mapNameForMap(location.mapId)
        << " zone=" << zoneNameForMap(location.mapId)
        << " players=" << playerCount << "\n"
        << "POSITION player=" << location.playerId
        << " x=" << location.x
        << " y=" << location.y
        << " z=" << location.z
        << " o=" << location.o << "\n";
    return out.str();
}

std::string MapInstanceServiceContext::unitPayloadForMap(int mapId) {
    std::ostringstream out;
    for (const auto& unit : unitsForMap(mapId)) {
        out << unitLine(unit);
    }
    return out.str();
}

std::string MapInstanceServiceContext::unitLine(const WorldUnitRecord& unit) {
    std::ostringstream out;
    out << "UNIT player=" << unit.id
        << " name=" << unit.name
        << " race=" << unit.race
        << " class=" << unit.klass
        << " gender=None"
        << " faction=" << unit.faction
        << " level=" << unit.level
        << " hp=" << unit.hp
        << " hpMax=" << unit.hpMax
        << " power=" << unit.power
        << " powerMax=" << unit.powerMax
        << " x=" << unit.x
        << " y=" << unit.y
        << " z=" << unit.z
        << " o=" << unit.o
        << " hostile=" << (unit.hostile ? 1 : 0)
        << " dead=" << (unit.dead ? 1 : 0)
        << " self=0 selected=0\n";
    return out.str();
}

std::string MapInstanceServiceContext::sanitizeToken(std::string value) {
    std::replace(value.begin(), value.end(), ' ', '_');
    return value;
}

auto MapInstanceServiceContext::findUnit(MapInstanceState& instance, int unitId) -> WorldUnitRecord* {
    auto it = std::find_if(instance.units.begin(), instance.units.end(), [&](const WorldUnitRecord& unit) { return unit.id == unitId; });
    return it == instance.units.end() ? nullptr : &*it;
}

auto MapInstanceServiceContext::findUnit(const MapInstanceState& instance, int unitId) -> const WorldUnitRecord* {
    auto it = std::find_if(instance.units.begin(), instance.units.end(), [&](const WorldUnitRecord& unit) { return unit.id == unitId; });
    return it == instance.units.end() ? nullptr : &*it;
}

SkynetMessage MapInstanceServiceContext::makeReply(const SkynetMessage& request, WowRuntimeMessage runtime) const {
    SkynetMessage reply;
    reply.source = ServiceId::WowMapInstance;
    reply.destination = request.source;
    reply.kind = MessageKind::WowRuntime;
    reply.requestId = 0;
    reply.replyTo = request.requestId;
    reply.wowRuntime = std::move(runtime);
    return reply;
}

CombatServiceContext::CombatServiceContext()
    : RequestReplyServiceContext(ServiceId::WowCombat) {}

auto CombatServiceContext::mainLoop() -> Task {
    while (true) {
        SkynetMessage message = co_await nextMessage();
        if (message.kind != MessageKind::WowRuntime) {
            continue;
        }

        const WowRuntimeMessage& runtime = message.wowRuntime;
        WowRuntimeMessage reply;
        reply.fd = runtime.fd;
        reply.playerId = runtime.playerId;

        switch (runtime.op) {
        case WowRuntimeOp::CombatInitActor: {
            CombatActorState& actor = actors_[runtime.playerId];
            actor.playerId = runtime.playerId;
            actor.targetId = 0;
            actor.autoAttacking = false;
            actor.dead = false;
            actor.ghost = false;
            actor.hp = actor.hpMax = 100;
            actor.power = actor.powerMax = 100;
            reply.op = WowRuntimeOp::CombatInitActor;
            reply.success = true;
            reply.payload = "hp=100 hpMax=100 power=100 powerMax=100";
            break;
        }
        case WowRuntimeOp::CombatTargetRequest: {
            CombatActorState& actor = actors_[runtime.playerId];
            actor.playerId = runtime.playerId;
            actor.targetId = runtime.targetId;
            reply.op = WowRuntimeOp::CombatTargetResult;
            if (actor.targetId == 0) {
                reply.targetId = 0;
                reply.success = true;
                reply.characterName = "None";
                break;
            }
            SkynetMessage request;
            request.destination = ServiceId::WowMapInstance;
            request.kind = MessageKind::WowRuntime;
            request.wowRuntime.op = WowRuntimeOp::MapQueryUnitRequest;
            request.wowRuntime.fd = runtime.fd;
            request.wowRuntime.playerId = runtime.playerId;
            request.wowRuntime.targetId = actor.targetId;
            SkynetMessage response = co_await callService(std::move(request));
            reply.targetId = actor.targetId;
            reply.success = response.wowRuntime.success;
            reply.characterName = response.wowRuntime.characterName;
            reply.value0 = response.wowRuntime.value0;
            reply.payload = response.wowRuntime.payload;
            if (!reply.success) {
                reply.reason = response.wowRuntime.reason;
            }
            break;
        }
        case WowRuntimeOp::CombatAttackRequest: {
            CombatActorState& actor = actors_[runtime.playerId];
            reply.op = WowRuntimeOp::CombatAttackResult;
            reply.targetId = actor.targetId;
            if (actor.dead || actor.ghost) {
                reply.success = false;
                reply.reason = "dead";
                break;
            }
            if (actor.targetId == 0) {
                reply.success = false;
                reply.reason = "target_required";
                break;
            }
            actor.autoAttacking = true;
            actor.power = std::max(0, actor.power - 5);
            const int damage = 12;
            SkynetMessage request;
            request.destination = ServiceId::WowMapInstance;
            request.kind = MessageKind::WowRuntime;
            request.wowRuntime.op = WowRuntimeOp::MapDamageUnitRequest;
            request.wowRuntime.fd = runtime.fd;
            request.wowRuntime.playerId = runtime.playerId;
            request.wowRuntime.targetId = actor.targetId;
            request.wowRuntime.value0 = damage;
            SkynetMessage response = co_await callService(std::move(request));
            reply.success = response.wowRuntime.success;
            reply.value0 = damage;
            reply.value1 = response.wowRuntime.value1;
            reply.payload = response.wowRuntime.payload;
            if (!reply.success) {
                reply.reason = response.wowRuntime.reason;
            }
            break;
        }
        case WowRuntimeOp::CombatCastRequest: {
            CombatActorState& actor = actors_[runtime.playerId];
            reply.op = WowRuntimeOp::CombatCastResult;
            reply.targetId = actor.targetId;
            reply.characterName = runtime.characterName;
            if (actor.dead || actor.ghost) {
                reply.success = false;
                reply.reason = "dead";
                break;
            }
            if (actor.targetId == 0) {
                reply.success = false;
                reply.reason = "target_required";
                break;
            }
            actor.power = std::max(0, actor.power - 12);
            SkynetMessage request;
            request.destination = ServiceId::WowMapInstance;
            request.kind = MessageKind::WowRuntime;
            request.wowRuntime.op = WowRuntimeOp::MapDamageUnitRequest;
            request.wowRuntime.fd = runtime.fd;
            request.wowRuntime.playerId = runtime.playerId;
            request.wowRuntime.targetId = actor.targetId;
            request.wowRuntime.value0 = 18;
            SkynetMessage response = co_await callService(std::move(request));
            reply.success = response.wowRuntime.success;
            reply.value0 = 18;
            reply.value1 = response.wowRuntime.value1;
            reply.payload = response.wowRuntime.payload;
            if (!reply.success) {
                reply.reason = response.wowRuntime.reason;
            }
            break;
        }
        case WowRuntimeOp::CombatLootRequest: {
            reply.op = WowRuntimeOp::CombatLootResult;
            SkynetMessage request;
            request.destination = ServiceId::WowMapInstance;
            request.kind = MessageKind::WowRuntime;
            request.wowRuntime.op = WowRuntimeOp::MapLootUnitRequest;
            request.wowRuntime.fd = runtime.fd;
            request.wowRuntime.playerId = runtime.playerId;
            request.wowRuntime.targetId = runtime.targetId;
            SkynetMessage response = co_await callService(std::move(request));
            reply.targetId = runtime.targetId;
            reply.success = response.wowRuntime.success;
            reply.value0 = response.wowRuntime.value0;
            reply.payload = response.wowRuntime.payload;
            if (!reply.success) {
                reply.reason = response.wowRuntime.reason;
            }
            break;
        }
        case WowRuntimeOp::CombatReleaseRequest: {
            CombatActorState& actor = actors_[runtime.playerId];
            actor.dead = true;
            actor.ghost = true;
            actor.autoAttacking = false;
            reply.op = WowRuntimeOp::CombatReleaseResult;
            reply.success = true;
            break;
        }
        case WowRuntimeOp::CombatResurrectRequest: {
            CombatActorState& actor = actors_[runtime.playerId];
            actor.dead = false;
            actor.ghost = false;
            actor.autoAttacking = false;
            actor.hp = actor.hpMax;
            actor.power = actor.powerMax;
            reply.op = WowRuntimeOp::CombatResurrectResult;
            reply.success = true;
            reply.payload = "hp=100 hpMax=100 power=100 powerMax=100";
            break;
        }
        case WowRuntimeOp::Tick: {
            reply.op = WowRuntimeOp::Tick;
            reply.success = true;
            break;
        }
        default:
            continue;
        }

        server().sendToService(message.source, makeReply(message, std::move(reply)));
    }
}

SkynetMessage CombatServiceContext::makeReply(const SkynetMessage& request, WowRuntimeMessage runtime) const {
    SkynetMessage reply;
    reply.source = ServiceId::WowCombat;
    reply.destination = request.source;
    reply.kind = MessageKind::WowRuntime;
    reply.requestId = 0;
    reply.replyTo = request.requestId;
    reply.wowRuntime = std::move(runtime);
    return reply;
}

} // namespace wow
