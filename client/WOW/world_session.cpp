#include "world_session.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <unordered_map>

namespace wowclient {

namespace {

std::unordered_map<std::string, std::string> parseKeyValues(const std::string& line) {
    std::unordered_map<std::string, std::string> fields;
    std::istringstream in(line);
    std::string token;
    in >> token;
    while (in >> token) {
        const std::size_t pos = token.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        fields[token.substr(0, pos)] = token.substr(pos + 1);
    }
    return fields;
}

int parseIntField(const std::unordered_map<std::string, std::string>& fields, const char* key, int fallback = 0) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return fallback;
    }
    return std::atoi(it->second.c_str());
}

float parseFloatField(const std::unordered_map<std::string, std::string>& fields, const char* key, float fallback = 0.0f) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return fallback;
    }
    return std::strtof(it->second.c_str(), nullptr);
}

std::string parseStringField(const std::unordered_map<std::string, std::string>& fields, const char* key, std::string fallback = {}) {
    const auto it = fields.find(key);
    return it == fields.end() ? std::move(fallback) : it->second;
}

std::vector<std::string> splitCommaList(const std::string& value) {
    std::vector<std::string> items;
    std::istringstream in(value);
    std::string item;
    while (std::getline(in, item, ',')) {
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
    }
    return items;
}

void setNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

} // namespace

WorldSession::~WorldSession() {
    disconnect();
}

bool WorldSession::connectNow(const std::string& host, int port) {
    disconnect();
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) {
        status_ = "socket failed";
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        status_ = "bad host";
        close(fd_);
        fd_ = -1;
        return false;
    }

    if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0 && errno != EINPROGRESS) {
        status_ = std::string("connect failed: ") + std::strerror(errno);
        close(fd_);
        fd_ = -1;
        return false;
    }

    setNonBlocking(fd_);
    connected_ = true;
    authenticated_ = false;
    enteringWorld_ = false;
    worldEntered_ = false;
    partial_.clear();
    writeBuffer_.clear();
    rawLog_.clear();
    realms_.clear();
    characters_.clear();
    nearbyUnits_.clear();
    chatLog_.clear();
    actionBar_.clear();
    quests_.clear();
    spells_.clear();
    vendorItems_.clear();
    gossipOptions_.clear();
    whoPlayers_.clear();
    inventory_.detailedItems.clear();
    inventory_.items.clear();
    authFailure_.clear();
    selfPlayerId_ = 0;
    playerCount_ = 0;
    instanceId_ = 0;
    queuedCommandCount_ = 0;
    worldName_ = "Azeroth";
    motd_ = "Connected. Waiting for realm greeting.";
    status_ = "connected";
    return true;
}

void WorldSession::disconnect() {
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
    connected_ = false;
    authenticated_ = false;
    enteringWorld_ = false;
    worldEntered_ = false;
    partial_.clear();
    writeBuffer_.clear();
}

void WorldSession::poll() {
    if (!connected_) {
        return;
    }

    while (!writeBuffer_.empty()) {
        const ssize_t n = send(fd_, writeBuffer_.data(), writeBuffer_.size(), 0);
        if (n > 0) {
            writeBuffer_.erase(0, static_cast<std::size_t>(n));
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        status_ = "send failed";
        disconnect();
        return;
    }

    char buffer[4096];
    while (true) {
        const ssize_t n = recv(fd_, buffer, sizeof(buffer), 0);
        if (n > 0) {
            partial_.append(buffer, static_cast<std::size_t>(n));
            continue;
        }
        if (n == 0) {
            status_ = "server closed";
            disconnect();
            return;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        status_ = "recv failed";
        disconnect();
        return;
    }

    std::size_t pos = 0;
    while ((pos = partial_.find('\n')) != std::string::npos) {
        std::string line = partial_.substr(0, pos);
        partial_.erase(0, pos + 1);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            consumeLine(line);
        }
    }
}

void WorldSession::sendAuth(const std::string& username, const std::string& password) {
    sendLine("AUTH " + username + " " + password);
}

void WorldSession::requestRealmList() {
    sendLine("REALM_LIST");
}

void WorldSession::requestCharacterList() {
    sendLine("CHAR_LIST");
}

void WorldSession::createCharacter(const std::string& name, const std::string& race, const std::string& klass, const std::string& gender) {
    sendLine("CREATE_CHAR " + name + " " + race + " " + klass + " " + gender);
}

void WorldSession::enterWorld(const std::string& characterName) {
    enteringWorld_ = true;
    worldEntered_ = false;
    queuedCommandCount_ = 0;
    status_ = "ENTER_WORLD pending";
    sendLine("ENTER_WORLD " + characterName);
}

void WorldSession::requestState() {
    sendLine("STATE");
}

void WorldSession::ping() {
    sendLine("PING");
}

void WorldSession::help() {
    sendLine("HELP");
}

void WorldSession::quit() {
    status_ = "QUIT pending";
    sendLine("QUIT");
}

void WorldSession::sendCommand(const std::string& command) {
    sendLine(command);
}

void WorldSession::move(const std::string& direction, float amount) {
    sendLine("MOVE " + direction + " " + std::to_string(amount));
}

void WorldSession::target(int targetId) {
    sendLine("TARGET " + std::to_string(targetId));
}

void WorldSession::tabTarget() {
    sendLine("TAB_TARGET");
}

void WorldSession::attack() {
    sendLine("ATTACK");
}

void WorldSession::cast(const std::string& spell) {
    sendLine("CAST " + spell);
}

void WorldSession::equip(int itemId) {
    sendLine("EQUIP " + std::to_string(itemId));
}

void WorldSession::unequip(const std::string& slot) {
    sendLine("UNEQUIP " + slot);
}

void WorldSession::say(const std::string& text) {
    sendLine("SAY " + text);
}

void WorldSession::requestQuests() {
    sendLine("QUEST_LIST");
}

void WorldSession::requestSpellbook() {
    sendLine("SPELLBOOK");
}

void WorldSession::requestWho() {
    sendLine("WHO");
}

void WorldSession::acceptQuest(int questId) {
    sendLine("QUEST_ACCEPT " + std::to_string(questId));
}

void WorldSession::turnInQuest(int questId) {
    sendLine("QUEST_TURNIN " + std::to_string(questId));
}

void WorldSession::requestVendor() {
    sendLine("VENDOR_LIST");
}

void WorldSession::buyItem(int itemId) {
    sendLine("BUY " + std::to_string(itemId));
}

void WorldSession::requestGossip() {
    sendLine("GOSSIP");
}

void WorldSession::gossipSelect(int index) {
    sendLine("GOSSIP_SELECT " + std::to_string(index));
}

void WorldSession::loot(int targetId) {
    sendLine("LOOT " + std::to_string(targetId));
}

void WorldSession::teleport(int mapId) {
    sendLine("TELEPORT " + std::to_string(mapId));
}

void WorldSession::train(const std::string& spell) {
    sendLine("TRAIN " + spell);
}

void WorldSession::release() {
    sendLine("RELEASE");
}

void WorldSession::resurrect() {
    sendLine("RESURRECT");
}

bool WorldSession::connected() const noexcept { return connected_; }
bool WorldSession::authenticated() const noexcept { return authenticated_; }
bool WorldSession::enteringWorld() const noexcept { return enteringWorld_; }
bool WorldSession::worldEntered() const noexcept { return worldEntered_; }
int WorldSession::selfPlayerId() const noexcept { return selfPlayerId_; }
int WorldSession::playerCount() const noexcept { return playerCount_; }
int WorldSession::instanceId() const noexcept { return instanceId_; }
int WorldSession::queuedCommandCount() const noexcept { return queuedCommandCount_; }
const std::string& WorldSession::worldName() const noexcept { return worldName_; }
const std::string& WorldSession::motd() const noexcept { return motd_; }
const std::string& WorldSession::status() const noexcept { return status_; }
const std::string& WorldSession::authFailure() const noexcept { return authFailure_; }
const std::string& WorldSession::accountName() const noexcept { return accountName_; }
const RealmSummary& WorldSession::selectedRealm() const noexcept { return selectedRealm_; }
const std::vector<RealmSummary>& WorldSession::realms() const noexcept { return realms_; }
const std::vector<CharacterSummary>& WorldSession::characters() const noexcept { return characters_; }
const CharacterSummary& WorldSession::activeCharacter() const noexcept { return activeCharacter_; }
const MapSnapshot& WorldSession::map() const noexcept { return map_; }
const UnitSnapshot& WorldSession::selfUnit() const noexcept { return selfUnit_; }
const std::vector<UnitSnapshot>& WorldSession::nearbyUnits() const noexcept { return nearbyUnits_; }
const PositionSnapshot& WorldSession::position() const noexcept { return position_; }
const TargetSnapshot& WorldSession::target() const noexcept { return target_; }
const CombatSnapshot& WorldSession::combat() const noexcept { return combat_; }
const InventorySnapshot& WorldSession::inventory() const noexcept { return inventory_; }
const std::vector<ChatMessage>& WorldSession::chatLog() const noexcept { return chatLog_; }
const std::vector<ActionButton>& WorldSession::actionBar() const noexcept { return actionBar_; }
const std::vector<QuestSummary>& WorldSession::quests() const noexcept { return quests_; }
const std::vector<SpellSummary>& WorldSession::spells() const noexcept { return spells_; }
const std::vector<VendorItem>& WorldSession::vendorItems() const noexcept { return vendorItems_; }
const std::vector<GossipOption>& WorldSession::gossipOptions() const noexcept { return gossipOptions_; }
const std::vector<WhoPlayerSummary>& WorldSession::whoPlayers() const noexcept { return whoPlayers_; }
const std::vector<std::string>& WorldSession::rawLog() const noexcept { return rawLog_; }

void WorldSession::sendLine(const std::string& line) {
    if (!connected_) {
        return;
    }
    writeBuffer_ += line;
    writeBuffer_.push_back('\n');
}

void WorldSession::consumeLine(const std::string& line) {
    rawLog_.push_back(line);
    if (rawLog_.size() > 300) {
        rawLog_.erase(rawLog_.begin(), rawLog_.begin() + static_cast<long>(rawLog_.size() - 300));
    }

    if (line.rfind("WELCOME", 0) == 0) {
        const auto fields = parseKeyValues(line);
        worldName_ = parseStringField(fields, "world", worldName_);
        selectedRealm_.name = parseStringField(fields, "realm", selectedRealm_.name);
        motd_ = "Connected to " + worldName_ + ".";
        status_ = line;
        return;
    }
    if (line.rfind("AUTH_OK", 0) == 0) {
        const auto fields = parseKeyValues(line);
        authenticated_ = true;
        enteringWorld_ = false;
        authFailure_.clear();
        accountName_ = parseStringField(fields, "user", accountName_);
        status_ = line;
        return;
    }
    if (line.rfind("AUTH_FAIL", 0) == 0) {
        const auto fields = parseKeyValues(line);
        authenticated_ = false;
        enteringWorld_ = false;
        authFailure_ = parseStringField(fields, "reason", "auth_failed");
        status_ = line;
        return;
    }
    if (line.rfind("REALM_LIST", 0) == 0) {
        realms_.clear();
        status_ = line;
        return;
    }
    if (line.rfind("REALM ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        RealmSummary realm;
        realm.name = parseStringField(fields, "name", "LocalDev");
        realm.address = parseStringField(fields, "address", "127.0.0.1");
        realm.population = parseStringField(fields, "population", "low");
        realm.characters = parseIntField(fields, "characters", 0);
        realms_.push_back(realm);
        if (selectedRealm_.name == "LocalDev" || selectedRealm_.name == realm.name) {
            selectedRealm_ = realm;
        }
        return;
    }
    if (line.rfind("CHAR_LIST", 0) == 0) {
        characters_.clear();
        status_ = line;
        return;
    }
    if (line.rfind("CHAR ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        CharacterSummary summary;
        summary.name = parseStringField(fields, "name", "ShellAdventurer");
        summary.race = parseStringField(fields, "race", "Human");
        summary.klass = parseStringField(fields, "class", "Warrior");
        summary.gender = parseStringField(fields, "gender", "Nonbinary");
        summary.level = parseIntField(fields, "level", 1);
        summary.zone = parseStringField(fields, "zone", "Northshire");
        characters_.push_back(summary);
        if (characters_.size() == 1) {
            activeCharacter_ = summary;
        }
        return;
    }
    if (line.rfind("CHAR_ENTER", 0) == 0) {
        const auto fields = parseKeyValues(line);
        enteringWorld_ = false;
        worldEntered_ = true;
        queuedCommandCount_ = 0;
        selfPlayerId_ = parseIntField(fields, "player", selfPlayerId_);
        instanceId_ = parseIntField(fields, "instance", instanceId_);
        activeCharacter_.name = parseStringField(fields, "name", activeCharacter_.name);
        activeCharacter_.zone = parseStringField(fields, "zone", activeCharacter_.zone);
        map_.zone = activeCharacter_.zone;
        status_ = line;
        return;
    }
    if (line.rfind("QUEUED ", 0) == 0) {
        ++queuedCommandCount_;
        status_ = line;
        return;
    }
    if (line.rfind("MAP ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        map_.id = parseIntField(fields, "id", 0);
        map_.name = parseStringField(fields, "name", map_.name);
        map_.zone = parseStringField(fields, "zone", map_.zone);
        return;
    }
    if (line.rfind("MAP_INSTANCE ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        map_.id = parseIntField(fields, "map", map_.id);
        instanceId_ = parseIntField(fields, "instance", instanceId_);
        map_.name = parseStringField(fields, "name", map_.name);
        map_.zone = parseStringField(fields, "zone", map_.zone);
        playerCount_ = parseIntField(fields, "players", playerCount_);
        return;
    }
    if (line.rfind("UNIT ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        UnitSnapshot unit;
        unit.playerId = parseIntField(fields, "player");
        unit.name = parseStringField(fields, "name", unit.name);
        unit.race = parseStringField(fields, "race", unit.race);
        unit.klass = parseStringField(fields, "class", unit.klass);
        unit.gender = parseStringField(fields, "gender", unit.gender);
        unit.faction = parseStringField(fields, "faction", unit.faction);
        unit.level = parseIntField(fields, "level", unit.level);
        unit.hp = parseIntField(fields, "hp", unit.hp);
        unit.hpMax = parseIntField(fields, "hpMax", unit.hpMax);
        unit.power = parseIntField(fields, "power", unit.power);
        unit.powerMax = parseIntField(fields, "powerMax", unit.powerMax);
        unit.x = parseFloatField(fields, "x", unit.x);
        unit.y = parseFloatField(fields, "y", unit.y);
        unit.z = parseFloatField(fields, "z", unit.z);
        unit.o = parseFloatField(fields, "o", unit.o);
        unit.hostile = parseIntField(fields, "hostile", 0) != 0;
        unit.dead = parseIntField(fields, "dead", 0) != 0;
        unit.selected = parseIntField(fields, "selected", 0) != 0;
        unit.self = parseIntField(fields, "self", 0) != 0;
        if (unit.self) {
            selfUnit_ = unit;
            selfPlayerId_ = unit.playerId;
            activeCharacter_.name = unit.name;
            activeCharacter_.race = unit.race;
            activeCharacter_.klass = unit.klass;
            activeCharacter_.gender = unit.gender;
            activeCharacter_.level = unit.level;
            position_.playerId = unit.playerId;
            position_.x = unit.x;
            position_.y = unit.y;
            position_.z = unit.z;
            position_.o = unit.o;
        } else {
            auto it = std::find_if(nearbyUnits_.begin(), nearbyUnits_.end(), [&](const UnitSnapshot& value) {
                return value.playerId == unit.playerId;
            });
            if (it == nearbyUnits_.end()) {
                nearbyUnits_.push_back(unit);
            } else {
                *it = unit;
            }
        }
        return;
    }
    if (line.rfind("CHAT ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        ChatMessage message;
        message.channel = parseStringField(fields, "channel", "SYSTEM");
        message.from = parseStringField(fields, "from", "World");
        message.text = parseStringField(fields, "text", "");
        chatLog_.push_back(std::move(message));
        if (chatLog_.size() > 200) {
            chatLog_.erase(chatLog_.begin(), chatLog_.begin() + static_cast<long>(chatLog_.size() - 200));
        }
        return;
    }
    if (line.rfind("POSITION ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        position_.playerId = parseIntField(fields, "player", position_.playerId);
        position_.x = parseFloatField(fields, "x", position_.x);
        position_.y = parseFloatField(fields, "y", position_.y);
        position_.z = parseFloatField(fields, "z", position_.z);
        position_.o = parseFloatField(fields, "o", position_.o);
        selfUnit_.x = position_.x;
        selfUnit_.y = position_.y;
        selfUnit_.z = position_.z;
        selfUnit_.o = position_.o;
        return;
    }
    if (line.rfind("TARGET ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        target_.playerId = parseIntField(fields, "player", target_.playerId);
        target_.targetId = parseIntField(fields, "target", target_.targetId);
        target_.hostile = parseIntField(fields, "hostile", 0) != 0;
        target_.name = parseStringField(fields, "name", target_.name);
        for (UnitSnapshot& unit : nearbyUnits_) {
            unit.selected = unit.playerId == target_.targetId;
        }
        return;
    }
    if (line.rfind("COMBAT ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        combat_.playerId = parseIntField(fields, "player", combat_.playerId);
        combat_.targetId = parseIntField(fields, "target", combat_.targetId);
        combat_.autoAttacking = parseIntField(fields, "auto", 0) != 0;
        combat_.dead = parseIntField(fields, "dead", 0) != 0;
        selfUnit_.dead = combat_.dead;
        return;
    }
    if (line.rfind("INVENTORY ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        inventory_.slots = parseIntField(fields, "slots", 16);
        inventory_.equipped = parseIntField(fields, "equipped", 0);
        inventory_.gold = parseIntField(fields, "gold", inventory_.gold);
        inventory_.items = splitCommaList(parseStringField(fields, "items", ""));
        inventory_.detailedItems.clear();
        return;
    }
    if (line.rfind("ITEM ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        ItemSnapshot item;
        item.id = parseIntField(fields, "id", 0);
        item.name = parseStringField(fields, "name", "");
        item.type = parseStringField(fields, "type", item.type);
        item.slot = parseStringField(fields, "slot", item.slot);
        item.bagSlot = parseIntField(fields, "bagSlot", item.bagSlot);
        item.equipped = parseIntField(fields, "equipped", 0) != 0;
        item.quality = parseIntField(fields, "quality", item.quality);
        item.stack = parseIntField(fields, "stack", item.stack);
        item.minDamage = parseIntField(fields, "minDamage", item.minDamage);
        item.maxDamage = parseIntField(fields, "maxDamage", item.maxDamage);
        item.speed = parseFloatField(fields, "speed", item.speed);
        inventory_.detailedItems.push_back(std::move(item));
        return;
    }
    if (line.rfind("ACTION ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        ActionButton action;
        action.slot = parseIntField(fields, "slot", 0);
        action.label = parseStringField(fields, "label", "");
        action.command = parseStringField(fields, "command", "");
        auto it = std::find_if(actionBar_.begin(), actionBar_.end(), [&](const ActionButton& value) { return value.slot == action.slot; });
        if (it == actionBar_.end()) {
            actionBar_.push_back(std::move(action));
        } else {
            *it = std::move(action);
        }
        std::sort(actionBar_.begin(), actionBar_.end(), [](const ActionButton& a, const ActionButton& b) { return a.slot < b.slot; });
        return;
    }
    if (line.rfind("SPELLBOOK ", 0) == 0) {
        spells_.clear();
        return;
    }
    if (line.rfind("SPELL ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        SpellSummary spell;
        spell.id = parseIntField(fields, "id", 0);
        spell.name = parseStringField(fields, "name", "");
        spell.cost = parseIntField(fields, "cost", 0);
        spell.minDamage = parseIntField(fields, "minDamage", 0);
        spell.maxDamage = parseIntField(fields, "maxDamage", 0);
        spell.cooldownMs = parseIntField(fields, "cooldownMs", 0);
        spell.remainingMs = parseIntField(fields, "remainingMs", 0);
        spells_.push_back(std::move(spell));
        return;
    }
    if (line.rfind("QUEST_LIST ", 0) == 0) {
        quests_.clear();
        return;
    }
    if (line.rfind("QUEST ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        QuestSummary quest;
        quest.id = parseIntField(fields, "id", 0);
        quest.title = parseStringField(fields, "title", "");
        quest.status = parseStringField(fields, "status", quest.status);
        quest.progress = parseIntField(fields, "progress", 0);
        quest.required = parseIntField(fields, "required", 1);
        quests_.push_back(std::move(quest));
        return;
    }
    if (line.rfind("VENDOR_LIST ", 0) == 0) {
        vendorItems_.clear();
        return;
    }
    if (line.rfind("VENDOR_ITEM ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        VendorItem item;
        item.id = parseIntField(fields, "id", 0);
        item.name = parseStringField(fields, "name", "");
        item.price = parseIntField(fields, "price", 0);
        item.type = parseStringField(fields, "type", item.type);
        vendorItems_.push_back(std::move(item));
        return;
    }
    if (line.rfind("GOSSIP ", 0) == 0) {
        gossipOptions_.clear();
        return;
    }
    if (line.rfind("GOSSIP_OPTION ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        GossipOption option;
        option.index = parseIntField(fields, "index", 0);
        option.text = parseStringField(fields, "text", "");
        gossipOptions_.push_back(std::move(option));
        return;
    }
    if (line.rfind("WHO ", 0) == 0) {
        whoPlayers_.clear();
        playerCount_ = parseIntField(parseKeyValues(line), "count", playerCount_);
        return;
    }
    if (line.rfind("WHO_PLAYER ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        WhoPlayerSummary player;
        player.id = parseIntField(fields, "id", 0);
        player.name = parseStringField(fields, "name", "");
        player.klass = parseStringField(fields, "class", "");
        player.level = parseIntField(fields, "level", 0);
        player.zone = parseStringField(fields, "zone", "");
        whoPlayers_.push_back(std::move(player));
        return;
    }
    if (line.rfind("CHAR_CREATE_OK", 0) == 0) {
        status_ = line;
        return;
    }
    if (line.rfind("MOVE_ACK ", 0) == 0) {
        status_ = line;
        return;
    }
    if (line.rfind("DAMAGE ", 0) == 0 || line.rfind("DEATH ", 0) == 0 || line.rfind("RESURRECT ", 0) == 0) {
        status_ = line;
        return;
    }
    if (line.rfind("WORLD ", 0) == 0) {
        const auto fields = parseKeyValues(line);
        worldName_ = parseStringField(fields, "name", worldName_);
        playerCount_ = parseIntField(fields, "players", playerCount_);
        return;
    }
    if (line.rfind("MOTD ", 0) == 0) {
        motd_ = line.substr(5);
        return;
    }
    if (line.rfind("BYE", 0) == 0) {
        status_ = line;
        disconnect();
        return;
    }
    status_ = line;
}

} // namespace wowclient
