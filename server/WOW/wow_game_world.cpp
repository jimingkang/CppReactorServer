#include "wow_game_world.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace wow {

namespace {

std::vector<std::string> splitWords(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> words;
    std::string word;
    while (in >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::string joinTail(const std::vector<std::string>& words, std::size_t start) {
    std::string value;
    for (std::size_t i = start; i < words.size(); ++i) {
        if (!value.empty()) {
            value.push_back(' ');
        }
        value += words[i];
    }
    return value;
}

std::string sanitizeToken(std::string value) {
    std::replace(value.begin(), value.end(), ' ', '_');
    return value;
}

std::vector<std::string> splitBy(std::string_view line, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : line) {
        if (ch == delimiter) {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    parts.push_back(current);
    return parts;
}

std::string toFixed(float value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << value;
    return out.str();
}

const char* weaponSlotForClass(const std::string& klass) {
    if (klass == "Mage") {
        return "MainHand";
    }
    return "MainHand";
}

} // namespace

WowGameWorld::WowGameWorld() {
    world_.initialize();
    seedWorldUnits();
}

std::vector<WowGameWorld::ItemState> WowGameWorld::starterItems() {
    return {
        {1001, "RustySword", "Weapon", "MainHand", 1, 1, 3, 6, 2.2f, true, -1},
        {1002, "TravelerTunic", "Armor", "Chest", 1, 1, 0, 0, 0.0f, true, -1},
        {1003, "Hearthstone", "Consumable", "None", 1, 1, 0, 0, 0.0f, false, 0},
        {1004, "TrainingPotion", "Consumable", "None", 1, 2, 0, 0, 0.0f, false, 1},
        {1005, "RecruitBoots", "Armor", "Feet", 1, 1, 0, 0, 0.0f, false, 2},
        {1006, "PracticeAxe", "Weapon", "MainHand", 2, 1, 4, 8, 2.8f, false, 3},
    };
}

std::vector<WowGameWorld::QuestState> WowGameWorld::starterQuests() {
    return {
        {2001, "Wolves_Across_Northshire", "progress", 1, 5},
        {2002, "Report_to_Marshal_McBride", "available", 0, 1},
    };
}

std::vector<WowGameWorld::SpellState> WowGameWorld::starterSpells(const std::string& klass) {
    if (klass == "Mage") {
        return {
            {4001, "Fireball", 20, 14, 22, 1500, 0},
            {4002, "Frostbolt", 15, 10, 16, 1200, 0},
        };
    }
    if (klass == "Rogue") {
        return {
            {4101, "SinisterStrike", 18, 12, 18, 1000, 0},
            {4102, "Throw", 8, 6, 10, 1500, 0},
        };
    }
    return {
        {4201, "HeroicStrike", 15, 12, 18, 1000, 0},
        {4202, "Charge", 10, 8, 12, 3000, 0},
    };
}

std::array<std::string, 13> WowGameWorld::starterActionBar() {
    return {"ATTACK", "CAST Fireball", "CAST HeroicStrike", "MOVE W", "MOVE S", "MOVE A", "MOVE D",
            "TARGET 9001", "TAB_TARGET", "SPELLBOOK", "QUEST_LIST", "VENDOR_LIST", "GOSSIP"};
}

void WowGameWorld::seedWorldUnits() {
    worldUnits_.clear();
    for (const auto& [id, creature] : world_.objectMgr().creatureTemplates()) {
        worldUnits_.emplace(id, WorldUnit{
            creature.id,
            creature.name,
            creature.race,
            creature.klass,
            creature.faction,
            creature.zone,
            creature.level,
            creature.hp,
            creature.hp,
            creature.power,
            creature.power,
            creature.x,
            creature.y,
            creature.z,
            creature.o,
            creature.hostile,
            false,
            creature.vendor,
            creature.gossip,
            creature.trainer,
            false,
            5 + (creature.level * 2),
            creature.vendor ? 0 : 1004,
            0,
            0,
            creature.x,
            creature.y,
            creature.z,
            creature.o,
        });
    }
}

WowGameWorld::PlayerState* WowGameWorld::findPlayer(int playerId) {
    const auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

const WowGameWorld::PlayerState* WowGameWorld::findPlayer(int playerId) const {
    const auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

WowGameWorld::WorldUnit* WowGameWorld::findUnit(int unitId) {
    const auto it = worldUnits_.find(unitId);
    return it == worldUnits_.end() ? nullptr : &it->second;
}

const WowGameWorld::WorldUnit* WowGameWorld::findUnit(int unitId) const {
    const auto it = worldUnits_.find(unitId);
    return it == worldUnits_.end() ? nullptr : &it->second;
}

WowGameWorld::SpellState* WowGameWorld::findSpell(PlayerState& state, const std::string& name) {
    auto it = std::find_if(state.spells.begin(), state.spells.end(), [&](const SpellState& spell) {
        return spell.name == name;
    });
    return it == state.spells.end() ? nullptr : &*it;
}

int WowGameWorld::nextHostileTarget(int currentTarget) const {
    std::vector<int> ids;
    for (const auto& [id, unit] : worldUnits_) {
        if (unit.hostile && !unit.dead) {
            ids.push_back(id);
        }
    }
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) {
        return 0;
    }
    const auto it = std::find(ids.begin(), ids.end(), currentTarget);
    if (it == ids.end() || std::next(it) == ids.end()) {
        return ids.front();
    }
    return *std::next(it);
}

std::string WowGameWorld::commandHelp() const {
    return "COMMANDS HELP | PING | STATE | MOVE dir amt | TARGET id | TAB_TARGET | ATTACK | CAST spell | SPELLBOOK | EQUIP itemId | UNEQUIP slot | SAY text | WHO | QUEST_LIST | QUEST_ACCEPT id | QUEST_TURNIN id | VENDOR_LIST | BUY itemId | GOSSIP | GOSSIP_SELECT index | LOOT targetId | TELEPORT mapId | RELEASE | TRAIN spell | RESURRECT | QUIT\n";
}

GameResponse WowGameWorld::join(const GameCommand& command) {
    std::lock_guard lock(mutex_);
    const int playerId = command.playerId > 0 ? command.playerId : command.fd;
    PlayerState state;
    state.playerId = playerId;
    state.fd = command.fd;
    state.instanceId = command.roomId > 0 ? command.roomId : 1;
    state.mapId = world_.mapMgr().starterMapId();
    state.zone = world_.mapMgr().starterZoneName();
    if (!command.line.empty()) {
        const std::vector<std::string> fields = splitBy(command.line, '|');
        state.name = fields[0];
        if (fields.size() > 1) {
            state.race = fields[1];
        }
        if (fields.size() > 2) {
            state.klass = fields[2];
        }
        if (fields.size() > 3) {
            state.gender = fields[3];
        }
        if (fields.size() > 4) {
            state.level = std::max(1, std::atoi(fields[4].c_str()));
        }
        if (fields.size() > 5) {
            state.zone = fields[5];
        }
    }
    if (const auto* characterTemplate = world_.objectMgr().findCharacterTemplate(state.name)) {
        state.race = characterTemplate->race;
        state.klass = characterTemplate->klass;
        state.gender = characterTemplate->gender;
        state.level = characterTemplate->level;
        state.mapId = characterTemplate->mapId;
        state.zone = characterTemplate->zone;
        state.x = characterTemplate->x;
        state.y = characterTemplate->y;
        state.z = characterTemplate->z;
        state.o = characterTemplate->o;
    }
    if (state.level <= 1) {
        state.level = std::max(1, playerId % 20);
    }
    state.items = starterItems();
    state.quests = starterQuests();
    state.spells = starterSpells(state.klass);
    state.actionBar = starterActionBar();
    state.targetId = nextHostileTarget(0);
    players_[playerId] = state;
    world_.mapMgr().addPlayer(state.mapId, state.instanceId, playerId);

    GameResponse response;
    response.type = GameResponseType::Joined;
    response.fd = command.fd;
    response.playerId = playerId;
    response.roomId = state.instanceId;
    response.text = snapshotFor(playerId) +
                    "CHAT channel=SYSTEM from=World text=Entered_world\n" +
                    commandHelp();
    broadcastToMap(state.mapId, state.instanceId, playerId,
                   "CHAT channel=SYSTEM from=World text=" + sanitizeToken(state.name) + "_entered_world\n");
    return response;
}

GameResponse WowGameWorld::leave(const GameCommand& command) {
    std::lock_guard lock(mutex_);
    const PlayerState* state = findPlayer(command.playerId);
    if (state != nullptr) {
        world_.mapMgr().removePlayer(state->mapId, state->instanceId, command.playerId);
        broadcastToMap(state->mapId, state->instanceId, command.playerId,
                       "CHAT channel=SYSTEM from=World text=" + sanitizeToken(state->name) + "_left_world\n");
    }
    players_.erase(command.playerId);

    GameResponse response;
    response.type = GameResponseType::LeaveAck;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = command.roomId > 0 ? command.roomId : 1;
    response.text = "BYE player=" + std::to_string(command.playerId) + "\n";
    response.closeAfterSend = true;
    return response;
}

GameResponse WowGameWorld::handleCommand(const GameCommand& command) {
    std::lock_guard lock(mutex_);
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = command.roomId > 0 ? command.roomId : 1;

    PlayerState* state = findPlayer(command.playerId);
    if (state == nullptr) {
        response.text = "ERR player_not_found\n";
        return response;
    }

    const std::vector<std::string> words = splitWords(command.line);
    if (words.empty()) {
        response.text = "ERR empty_command\n";
        return response;
    }

    const std::string& op = words.front();
    if (op == "QUIT") {
        return leave(command);
    }
    if (op == "PING") {
        response.text = "PONG\n";
        return response;
    }
    if (op == "HELP") {
        response.text = commandHelp();
        return response;
    }
    response.text = "ERR unknown_command\n";
    return response;
}

std::string WowGameWorld::snapshot() const {
    std::lock_guard lock(mutex_);
    std::ostringstream out;
    out << world_.snapshot(players_.size());
    for (const auto& [playerId, state] : players_) {
        out << "PLAYER id=" << playerId
            << " fd=" << state.fd
            << " map=" << state.mapId
            << " instance=" << state.instanceId
            << " zone=" << state.zone << "\n";
    }
    return out.str();
}

std::vector<GameResponse> WowGameWorld::takePendingResponses() {
    std::lock_guard lock(mutex_);
    std::vector<GameResponse> out;
    out.swap(pendingResponses_);
    return out;
}

std::string WowGameWorld::snapshotFor(int playerId) const {
    std::ostringstream out;
    const PlayerState* self = findPlayer(playerId);
    if (self == nullptr) {
        return "ERR player_not_found\n";
    }

    out << world_.snapshot(players_.size());
    out << "MAP id=" << self->mapId
        << " name=" << (world_.mapMgr().findMap(self->mapId) ? world_.mapMgr().findMap(self->mapId)->name : world_.mapMgr().starterMapName())
        << " zone=" << self->zone << "\n";
    out << world_.mapMgr().instanceSummary(self->mapId, self->instanceId);

    for (const auto& [id, state] : players_) {
        if (state.mapId == self->mapId && state.instanceId == self->instanceId) {
            out << unitLineForPlayer(state, playerId);
        }
    }
    for (const auto& [id, unit] : worldUnits_) {
        out << unitLineForWorldUnit(unit, self->targetId);
    }

    out << movementSnapshot(*self);
    out << targetSnapshot(*self);
    out << combatSnapshot(*self);
    out << inventorySnapshot(*self);
    out << spellbookSnapshot(*self);
    out << questSnapshot(*self);
    out << vendorSnapshot();
    out << gossipSnapshot();
    out << "CHAT channel=SYSTEM from=World text=Welcome_to_" << world_.mapMgr().starterMapName() << "\n";
    return out.str();
}

std::string WowGameWorld::movementSnapshot(const PlayerState& state) const {
    return "POSITION player=" + std::to_string(state.playerId) +
           " x=" + toFixed(state.x) +
           " y=" + toFixed(state.y) +
           " z=" + toFixed(state.z) +
           " o=" + toFixed(state.o) + "\n";
}

std::string WowGameWorld::targetSnapshot(const PlayerState& state) const {
    std::ostringstream out;
    const WorldUnit* target = findUnit(state.targetId);
    out << "TARGET player=" << state.playerId
        << " target=" << state.targetId
        << " hostile=" << ((target != nullptr && target->hostile) ? 1 : 0)
        << " name=" << (target != nullptr ? target->name : "None") << "\n";
    return out.str();
}

std::string WowGameWorld::combatSnapshot(const PlayerState& state) const {
    std::ostringstream out;
    out << "COMBAT player=" << state.playerId
        << " target=" << state.targetId
        << " auto=" << (state.autoAttacking ? 1 : 0)
        << " dead=" << (state.dead ? 1 : 0) << "\n";
    return out.str();
}

std::string WowGameWorld::inventorySnapshot(const PlayerState& state) const {
    std::ostringstream out;
    int equipped = 0;
    for (const ItemState& item : state.items) {
        if (item.equipped) {
            ++equipped;
        }
    }
    out << "INVENTORY owner=" << state.playerId
        << " slots=16 equipped=" << equipped
        << " gold=" << state.gold
        << " items=";
    bool first = true;
    for (const ItemState& item : state.items) {
        if (!item.equipped && item.bagSlot >= 0) {
            if (!first) {
                out << ",";
            }
            out << item.name;
            first = false;
        }
    }
    out << "\n";
    for (const ItemState& item : state.items) {
        out << "ITEM id=" << item.id
            << " name=" << item.name
            << " type=" << item.type
            << " slot=" << item.equipSlot
            << " bagSlot=" << item.bagSlot
            << " equipped=" << (item.equipped ? 1 : 0)
            << " quality=" << item.quality
            << " stack=" << item.stack
            << " minDamage=" << item.minDamage
            << " maxDamage=" << item.maxDamage
            << " speed=" << toFixed(item.speed) << "\n";
    }
    for (std::size_t slot = 0; slot < state.actionBar.size(); ++slot) {
        out << "ACTION slot=" << (slot + 1)
            << " label=" << sanitizeToken(state.actionBar[slot])
            << " command=" << sanitizeToken(state.actionBar[slot]) << "\n";
    }
    return out.str();
}

std::string WowGameWorld::questSnapshot(const PlayerState& state) const {
    std::ostringstream out;
    out << "QUEST_LIST count=" << state.quests.size() << "\n";
    for (const QuestState& quest : state.quests) {
        out << "QUEST id=" << quest.id
            << " title=" << quest.title
            << " status=" << quest.status
            << " progress=" << quest.progress
            << " required=" << quest.required << "\n";
    }
    return out.str();
}

std::string WowGameWorld::spellbookSnapshot(const PlayerState& state) const {
    std::ostringstream out;
    out << "SPELLBOOK count=" << state.spells.size() << "\n";
    for (const SpellState& spell : state.spells) {
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

std::string WowGameWorld::vendorSnapshot() const {
    const auto& items = world_.objectMgr().vendorItems();
    std::ostringstream out;
    out << "VENDOR_LIST npc=Brother_Danil count=" << items.size() << "\n";
    for (const auto& item : items) {
        out << "VENDOR_ITEM id=" << item.id
            << " name=" << item.name
            << " price=" << item.price
            << " type=" << item.type << "\n";
    }
    return out.str();
}

std::string WowGameWorld::gossipSnapshot() const {
    const auto& options = world_.objectMgr().gossipOptions();
    std::ostringstream out;
    out << "GOSSIP npc=Marshal_McBride count=" << options.size() << "\n";
    for (const auto& entry : options) {
        out << "GOSSIP_OPTION index=" << entry.index
            << " text=" << entry.text << "\n";
    }
    return out.str();
}

std::string WowGameWorld::whoSnapshot(const PlayerState& state) const {
    std::ostringstream out;
    out << "WHO count=" << world_.mapMgr().playersInInstance(state.mapId, state.instanceId).size()
        << " map=" << state.mapId
        << " instance=" << state.instanceId << "\n";
    for (int playerId : world_.mapMgr().playersInInstance(state.mapId, state.instanceId)) {
        const PlayerState* other = findPlayer(playerId);
        if (other == nullptr) {
            continue;
        }
        out << "WHO_PLAYER id=" << other->playerId
            << " name=" << other->name
            << " class=" << other->klass
            << " level=" << other->level
            << " zone=" << other->zone << "\n";
    }
    return out.str();
}

void WowGameWorld::tick(int ms) {
    std::lock_guard lock(mutex_);
    for (auto& [_, player] : players_) {
        tickPlayer(player, ms);
    }

    for (auto& [_, unit] : worldUnits_) {
        tickUnit(unit, ms);
    }
}

void WowGameWorld::tickPlayer(PlayerState& state, int ms) {
    state.attackCooldownMs = std::max(0, state.attackCooldownMs - ms);
    state.globalCooldownMs = std::max(0, state.globalCooldownMs - ms);
    for (SpellState& spell : state.spells) {
        spell.remainingCooldownMs = std::max(0, spell.remainingCooldownMs - ms);
    }

    if (state.dead) {
        state.corpseReleaseMsRemaining = std::max(0, state.corpseReleaseMsRemaining - ms);
        return;
    }

    state.hp = std::min(state.hpMax, state.hp + std::max(1, ms / 500));
    state.power = std::min(state.powerMax, state.power + std::max(1, ms / 250));

    if (!state.autoAttacking || state.targetId == 0 || state.attackCooldownMs > 0) {
        return;
    }

    WorldUnit* target = findUnit(state.targetId);
    if (target == nullptr || target->dead || !target->hostile) {
        state.autoAttacking = false;
        return;
    }

    state.attackCooldownMs = 2000;
    const int damage = std::max(6, state.level + 4);
    target->hp = std::max(0, target->hp - damage);

    std::ostringstream out;
    out << "SWING source=" << state.playerId
        << " target=" << target->id
        << " amount=" << damage << "\n";

    if (target->hp == 0) {
        target->dead = true;
        target->lootAvailable = true;
        target->respawnMsRemaining = 5000;
        applyKillCredit(state, *target);
        out << "DEATH player=" << target->id << " kind=Creature\n";
    } else if (target->attackCooldownMs <= 0) {
        target->attackCooldownMs = 2000;
        const int retaliation = std::max(3, target->level + 1);
        state.hp = std::max(0, state.hp - retaliation);
        out << "SWING source=" << target->id
            << " target=" << state.playerId
            << " amount=" << retaliation << "\n";
        if (state.hp == 0) {
            state.dead = true;
            state.ghost = false;
            state.autoAttacking = false;
            state.corpseReleaseMsRemaining = 3000;
            state.corpseX = state.x;
            state.corpseY = state.y;
            state.corpseZ = state.z;
            out << "DEATH player=" << state.playerId << " kind=Spirit\n";
        }
    }

    out << unitLineForWorldUnit(*target, state.targetId);
    out << unitLineForPlayer(state, state.playerId);
    broadcastToMap(state.mapId, state.instanceId, 0, out.str());
}

void WowGameWorld::tickUnit(WorldUnit& unit, int ms) {
    unit.attackCooldownMs = std::max(0, unit.attackCooldownMs - ms);
    if (!unit.dead || unit.respawnMsRemaining <= 0) {
        return;
    }
    unit.respawnMsRemaining = std::max(0, unit.respawnMsRemaining - ms);
    if (unit.respawnMsRemaining == 0) {
        unit.dead = false;
        unit.hp = unit.hpMax;
        unit.lootAvailable = false;
        unit.x = unit.spawnX;
        unit.y = unit.spawnY;
        unit.z = unit.spawnZ;
        unit.o = unit.spawnO;
        broadcastToMap(0, 1, 0, "RESPAWN unit=" + std::to_string(unit.id) + "\n" + unitLineForWorldUnit(unit, 0));
    }
}

std::string WowGameWorld::unitLineForPlayer(const PlayerState& state, int selfPlayerId) const {
    std::ostringstream out;
    out << "UNIT player=" << state.playerId
        << " name=" << state.name
        << " race=" << state.race
        << " class=" << state.klass
        << " gender=" << state.gender
        << " faction=" << state.faction
        << " level=" << state.level
        << " hp=" << state.hp
        << " hpMax=" << state.hpMax
        << " power=" << state.power
        << " powerMax=" << state.powerMax
        << " x=" << toFixed(state.x)
        << " y=" << toFixed(state.y)
        << " z=" << toFixed(state.z)
        << " o=" << toFixed(state.o)
        << " hostile=0"
        << " dead=" << (state.dead ? 1 : 0)
        << " self=" << (state.playerId == selfPlayerId ? 1 : 0) << "\n";
    return out.str();
}

std::string WowGameWorld::unitLineForWorldUnit(const WorldUnit& unit, int targetId) const {
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
        << " x=" << toFixed(unit.x)
        << " y=" << toFixed(unit.y)
        << " z=" << toFixed(unit.z)
        << " o=" << toFixed(unit.o)
        << " hostile=" << (unit.hostile ? 1 : 0)
        << " dead=" << (unit.dead ? 1 : 0)
        << " self=0"
        << " selected=" << (unit.id == targetId ? 1 : 0) << "\n";
    return out.str();
}

GameResponse WowGameWorld::handleMove(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (state.dead) {
        response.text = "ERR dead_cannot_move\n";
        return response;
    }
    if (words.size() < 2) {
        response.text = "ERR usage MOVE dir amt\n";
        return response;
    }

    const std::string direction = words[1];
    const float amount = words.size() > 2 ? std::max(0.5f, std::stof(words[2])) : 2.0f;
    if (direction == "W" || direction == "N") {
        state.y += amount;
        state.o = 0.0f;
    } else if (direction == "S") {
        state.y -= amount;
        state.o = 3.14f;
    } else if (direction == "A") {
        state.x -= amount;
        state.o = -1.57f;
    } else if (direction == "D" || direction == "E") {
        state.x += amount;
        state.o = 1.57f;
    } else {
        response.text = "ERR bad_direction\n";
        return response;
    }

    response.text = movementSnapshot(state) + "MOVE_ACK player=" + std::to_string(state.playerId) + " dir=" + direction + "\n";
    broadcastToMap(state.mapId, state.instanceId, state.playerId,
                   "POSITION player=" + std::to_string(state.playerId) +
                   " x=" + toFixed(state.x) +
                   " y=" + toFixed(state.y) +
                   " z=" + toFixed(state.z) +
                   " o=" + toFixed(state.o) + "\n");
    return response;
}

GameResponse WowGameWorld::handleTarget(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) const {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;

    if (words.front() == "TAB_TARGET") {
        state.targetId = nextHostileTarget(state.targetId);
        response.text = targetSnapshot(state);
        return response;
    }
    if (words.size() < 2) {
        response.text = "ERR usage TARGET id\n";
        return response;
    }
    const int targetId = std::atoi(words[1].c_str());
    state.targetId = targetId;
    response.text = targetSnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleAttack(PlayerState& state, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;

    if (state.dead) {
        response.text = "ERR dead_cannot_attack\n";
        return response;
    }
    WorldUnit* target = findUnit(state.targetId);
    if (target == nullptr || target->dead) {
        response.text = "ERR no_live_target\n";
        return response;
    }
    if (!target->hostile) {
        response.text = "ERR target_not_hostile\n";
        return response;
    }

    state.autoAttacking = true;
    state.attackCooldownMs = 0;
    const int damage = state.level >= 8 ? 14 : 10;
    target->hp = std::max(0, target->hp - damage);

    std::ostringstream out;
    out << "COMBAT player=" << state.playerId
        << " target=" << state.targetId
        << " auto=1 dead=0\n";
    out << "DAMAGE source=" << state.playerId
        << " target=" << target->id
        << " amount=" << damage
        << " reason=AutoAttack\n";
    if (target->hp == 0) {
        target->dead = true;
        target->lootAvailable = true;
        target->respawnMsRemaining = 5000;
        applyKillCredit(state, *target);
        out << "DEATH player=" << target->id << " kind=Creature\n";
    } else {
        const int retaliation = 6;
        state.hp = std::max(0, state.hp - retaliation);
        out << "DAMAGE source=" << target->id
            << " target=" << state.playerId
            << " amount=" << retaliation
            << " reason=Retaliation\n";
        if (state.hp == 0) {
            state.dead = true;
            state.ghost = false;
            state.autoAttacking = false;
            state.corpseReleaseMsRemaining = 3000;
            state.corpseX = state.x;
            state.corpseY = state.y;
            state.corpseZ = state.z;
            out << "DEATH player=" << state.playerId << " kind=Spirit\n";
        }
    }
    out << unitLineForWorldUnit(*target, state.targetId);
    out << unitLineForPlayer(state, state.playerId);
    out << questSnapshot(state);
    response.text = out.str();
    broadcastToMap(state.mapId, state.instanceId, state.playerId, response.text);
    return response;
}

GameResponse WowGameWorld::handleCast(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage CAST spell\n";
        return response;
    }
    const std::string spell = words[1];
    SpellState* spellState = findSpell(state, spell);
    if (spellState == nullptr) {
        response.text = "ERR spell_unknown\n";
        return response;
    }
    if (state.dead || state.ghost) {
        response.text = "ERR dead_cannot_cast\n";
        return response;
    }
    if (state.globalCooldownMs > 0 || spellState->remainingCooldownMs > 0) {
        response.text = "ERR spell_on_cooldown\n";
        return response;
    }
    if (state.power < spellState->powerCost) {
        response.text = "ERR not_enough_power\n";
        return response;
    }
    WorldUnit* target = findUnit(state.targetId);
    if (target == nullptr || target->dead) {
        response.text = "ERR no_live_target\n";
        return response;
    }

    state.power -= spellState->powerCost;
    state.globalCooldownMs = 1000;
    spellState->remainingCooldownMs = spellState->cooldownMs;
    const int damage = spellState->minDamage + ((spellState->maxDamage - spellState->minDamage) / 2);
    target->hp = std::max(0, target->hp - damage);
    std::ostringstream out;
    out << "CHAT channel=SYSTEM from=Spell text=Cast_" << sanitizeToken(spellState->name) << "\n";
    out << "DAMAGE source=" << state.playerId << " target=" << target->id << " amount=" << damage << " reason=" << sanitizeToken(spellState->name) << "\n";
    if (target->hp == 0) {
        target->dead = true;
        target->lootAvailable = true;
        target->respawnMsRemaining = 5000;
        applyKillCredit(state, *target);
        out << "DEATH player=" << target->id << " kind=Creature\n";
    }
    out << unitLineForWorldUnit(*target, state.targetId);
    out << spellbookSnapshot(state);
    response.text = out.str();
    broadcastToMap(state.mapId, state.instanceId, state.playerId, response.text);
    return response;
}

GameResponse WowGameWorld::handleEquip(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage EQUIP itemId\n";
        return response;
    }
    const int itemId = std::atoi(words[1].c_str());
    auto it = std::find_if(state.items.begin(), state.items.end(), [&](const ItemState& item) { return item.id == itemId; });
    if (it == state.items.end()) {
        response.text = "ERR item_not_found\n";
        return response;
    }
    for (ItemState& item : state.items) {
        if (item.equipSlot == it->equipSlot) {
            item.equipped = false;
            if (item.bagSlot < 0) {
                item.bagSlot = 0;
            }
        }
    }
    it->equipped = true;
    it->bagSlot = -1;
    response.text = inventorySnapshot(state) + "CHAT channel=SYSTEM from=Inventory text=Equipped_" + it->name + "\n";
    return response;
}

GameResponse WowGameWorld::handleUnequip(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage UNEQUIP slot\n";
        return response;
    }
    const std::string slotName = words[1];
    auto it = std::find_if(state.items.begin(), state.items.end(), [&](const ItemState& item) {
        return item.equipped && item.equipSlot == slotName;
    });
    if (it == state.items.end()) {
        response.text = "ERR slot_empty\n";
        return response;
    }
    it->equipped = false;
    it->bagSlot = 0;
    response.text = inventorySnapshot(state) + "CHAT channel=SYSTEM from=Inventory text=Unequipped_" + it->name + "\n";
    return response;
}

GameResponse WowGameWorld::handleChat(PlayerState& state, const std::string& line, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    std::string text = line.size() > 4 ? line.substr(4) : std::string{};
    if (text.empty()) {
        response.text = "ERR usage SAY text\n";
        return response;
    }
    response.text = "CHAT channel=SAY from=" + state.name + " text=" + sanitizeToken(text) + "\n";
    broadcastToMap(state.mapId, state.instanceId, state.playerId, response.text);
    return response;
}

GameResponse WowGameWorld::handleQuestAccept(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage QUEST_ACCEPT id\n";
        return response;
    }
    const int questId = std::atoi(words[1].c_str());
    auto it = std::find_if(state.quests.begin(), state.quests.end(), [&](const QuestState& quest) { return quest.id == questId; });
    if (it == state.quests.end()) {
        response.text = "ERR quest_not_found\n";
        return response;
    }
    if (it->status != "available") {
        response.text = "ERR quest_not_available\n";
        return response;
    }
    it->status = "progress";
    response.text = "QUEST_ACCEPT id=" + std::to_string(it->id) + " title=" + it->title + "\n" + questSnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleQuestTurnIn(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage QUEST_TURNIN id\n";
        return response;
    }
    const int questId = std::atoi(words[1].c_str());
    auto it = std::find_if(state.quests.begin(), state.quests.end(), [&](const QuestState& quest) { return quest.id == questId; });
    if (it == state.quests.end()) {
        response.text = "ERR quest_not_found\n";
        return response;
    }
    if (it->status != "complete") {
        response.text = "ERR quest_not_complete\n";
        return response;
    }
    it->status = "rewarded";
    state.gold += 12;
    state.power = std::min(state.powerMax, state.power + 15);
    response.text = "QUEST_TURNIN id=" + std::to_string(it->id) + " reward_gold=12\n" + questSnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleLoot(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage LOOT targetId\n";
        return response;
    }
    WorldUnit* target = findUnit(std::atoi(words[1].c_str()));
    if (target == nullptr) {
        response.text = "ERR target_not_found\n";
        return response;
    }
    if (!target->dead || !target->lootAvailable) {
        response.text = "ERR no_loot_available\n";
        return response;
    }
    state.gold += target->lootGold;
    target->lootAvailable = false;
    if (target->lootItemId != 0) {
        for (ItemState& item : state.items) {
            if (item.id == target->lootItemId) {
                item.stack += 1;
                response.text = "LOOT target=" + std::to_string(target->id) +
                                " gold=" + std::to_string(target->lootGold) +
                                " item=" + item.name + "\n" + inventorySnapshot(state);
                return response;
            }
        }
    }
    response.text = "LOOT target=" + std::to_string(target->id) +
                    " gold=" + std::to_string(target->lootGold) + "\n" + inventorySnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleVendorBuy(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage BUY itemId\n";
        return response;
    }
    const int itemId = std::atoi(words[1].c_str());
    const auto& vendorItems = world_.objectMgr().vendorItems();
    const auto it = std::find_if(vendorItems.begin(), vendorItems.end(), [&](const auto& item) { return item.id == itemId; });
    if (it == vendorItems.end()) {
        response.text = "ERR vendor_item_not_found\n";
        return response;
    }
    if (state.gold < it->price) {
        response.text = "ERR not_enough_gold\n";
        return response;
    }
    state.gold -= it->price;
    state.items.push_back(ItemState{it->id, it->name, it->type, "None", 1, 1, 0, 0, 0.0f, false, static_cast<int>(state.items.size())});
    response.text = "BUY_OK item=" + it->name + " price=" + std::to_string(it->price) + "\n" + inventorySnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleGossipSelect(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) const {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage GOSSIP_SELECT index\n";
        return response;
    }
    const int index = std::atoi(words[1].c_str());
    const auto& gossipOptions = world_.objectMgr().gossipOptions();
    if (index < 0 || static_cast<std::size_t>(index) >= gossipOptions.size()) {
        response.text = "ERR gossip_option_not_found\n";
        return response;
    }
    response.text = "GOSSIP_RESULT index=" + std::to_string(index) + " text=" + gossipOptions[static_cast<std::size_t>(index)].text + "\n";
    return response;
}

GameResponse WowGameWorld::handleTeleport(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage TELEPORT mapId\n";
        return response;
    }
    const int mapId = std::atoi(words[1].c_str());
    const auto* map = world_.mapMgr().findMap(mapId);
    if (map == nullptr) {
        response.text = "ERR map_not_found\n";
        return response;
    }
    const int oldMapId = state.mapId;
    const int oldInstanceId = state.instanceId;
    world_.mapMgr().removePlayer(oldMapId, oldInstanceId, state.playerId);
    state.mapId = mapId;
    state.zone = map->zone;
    state.x = map->originX;
    state.y = map->originY;
    state.o = 0.0f;
    world_.mapMgr().addPlayer(state.mapId, state.instanceId, state.playerId);
    response.text = "TELEPORT player=" + std::to_string(state.playerId) +
                    " map=" + std::to_string(state.mapId) +
                    " zone=" + state.zone + "\n" +
                    world_.mapMgr().instanceSummary(state.mapId, state.instanceId) +
                    movementSnapshot(state);
    broadcastToMap(oldMapId, oldInstanceId, state.playerId,
                   "CHAT channel=SYSTEM from=World text=" + sanitizeToken(state.name) + "_left_map\n");
    broadcastToMap(state.mapId, state.instanceId, state.playerId,
                   "CHAT channel=SYSTEM from=World text=" + sanitizeToken(state.name) + "_entered_map\n");
    return response;
}

GameResponse WowGameWorld::handleRelease(PlayerState& state, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (!state.dead) {
        response.text = "ERR not_dead\n";
        return response;
    }
    if (state.corpseReleaseMsRemaining > 0) {
        response.text = "ERR release_pending ms=" + std::to_string(state.corpseReleaseMsRemaining) + "\n";
        return response;
    }
    state.ghost = true;
    state.x = state.corpseX + 2.0f;
    state.y = state.corpseY + 2.0f;
    response.text = "RELEASE player=" + std::to_string(state.playerId) + "\n" +
                    movementSnapshot(state) +
                    combatSnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleTrain(PlayerState& state, const std::vector<std::string>& words, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (words.size() < 2) {
        response.text = "ERR usage TRAIN spell\n";
        return response;
    }
    if (state.gold < 10) {
        response.text = "ERR not_enough_gold\n";
        return response;
    }
    const std::string spellName = words[1];
    if (findSpell(state, spellName) != nullptr) {
        response.text = "ERR spell_already_known\n";
        return response;
    }
    state.gold -= 10;
    state.spells.push_back(SpellState{5000 + static_cast<int>(state.spells.size()) + 1, spellName, 12, 9, 15, 1500, 0});
    response.text = "TRAIN_OK spell=" + spellName + " price=10\n" + spellbookSnapshot(state) + inventorySnapshot(state);
    return response;
}

GameResponse WowGameWorld::handleResurrect(PlayerState& state, const GameCommand& command) {
    GameResponse response;
    response.type = GameResponseType::Text;
    response.fd = command.fd;
    response.playerId = command.playerId;
    response.roomId = state.instanceId;
    if (!state.dead && !state.ghost) {
        response.text = "ERR not_dead\n";
        return response;
    }
    state.dead = false;
    state.ghost = false;
    state.hp = state.hpMax;
    state.power = state.powerMax;
    state.corpseReleaseMsRemaining = 0;
    response.text = "RESURRECT player=" + std::to_string(state.playerId) + " by=SpiritHealer\n" +
                    unitLineForPlayer(state, state.playerId) +
                    combatSnapshot(state);
    broadcastToMap(state.mapId, state.instanceId, state.playerId, response.text);
    return response;
}

void WowGameWorld::broadcastToMap(int mapId, int instanceId, int exceptPlayerId, std::string text) {
    for (int playerId : world_.mapMgr().playersInInstance(mapId, instanceId)) {
        if (playerId == exceptPlayerId) {
            continue;
        }
        const PlayerState* state = findPlayer(playerId);
        if (state == nullptr) {
            continue;
        }
        GameResponse response;
        response.type = GameResponseType::Text;
        response.fd = state->fd;
        response.playerId = state->playerId;
        response.roomId = state->instanceId;
        response.text = text;
        pendingResponses_.push_back(std::move(response));
    }
}

void WowGameWorld::applyKillCredit(PlayerState& state, const WorldUnit& target) {
    if (!state.quests.empty() && state.quests.front().status == "progress" &&
        state.quests.front().progress < state.quests.front().required) {
        state.quests.front().progress += 1;
        if (state.quests.front().progress >= state.quests.front().required) {
            state.quests.front().status = "complete";
        }
    }
    state.gold += target.lootGold;
}

} // namespace wow
