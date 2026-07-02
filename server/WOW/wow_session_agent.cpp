#include "wow_session_agent.h"

#include <sstream>

namespace wow {

namespace {

SkynetMessage wrapLogin(LoginMessage login) {
    SkynetMessage message;
    message.destination = ServiceId::Login;
    message.kind = MessageKind::Login;
    message.login = std::move(login);
    return message;
}

SkynetMessage wrapWorld(GameCommand command) {
    SkynetMessage message;
    message.destination = ServiceId::GameWorld;
    message.kind = MessageKind::GameCommand;
    message.gameCommand = std::move(command);
    return message;
}

SkynetMessage wrapWowRuntime(ServiceId destination, WowRuntimeMessage runtime) {
    SkynetMessage message;
    message.destination = destination;
    message.kind = MessageKind::WowRuntime;
    message.wowRuntime = std::move(runtime);
    return message;
}

} // namespace

WowSessionAgent::WowSessionAgent(int fd)
    : session_(fd) {}

int WowSessionAgent::fd() const noexcept {
    return session_.fd();
}

int WowSessionAgent::playerId() const noexcept {
    return session_.playerId();
}

bool WowSessionAgent::closing() const noexcept {
    return session_.closing();
}

SessionActions WowSessionAgent::onAccept() {
    SessionActions actions;
    actions.socketCommands.push_back(SocketCommand{
        SocketCommandType::Send,
        session_.fd(),
        "WELCOME world=wow-shell realm=LocalDev\n"
    });
    return actions;
}

SessionActions WowSessionAgent::onSocketData(std::string_view chunk) {
    input_.append(chunk.data(), chunk.size());
    SessionActions actions;

    std::size_t pos = 0;
    while ((pos = input_.find('\n')) != std::string::npos) {
        std::string line = trimLine(input_.substr(0, pos + 1));
        input_.erase(0, pos + 1);
        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> words = splitWords(line);
        if (words.empty()) {
            continue;
        }
        const WowOpcode opcode = socketRouter_.route(line);

        if (opcode == WowOpcode::Ping) {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "PONG\n"});
            continue;
        }
        if (opcode == WowOpcode::Help) {
            actions.socketCommands.push_back(SocketCommand{
                SocketCommandType::Send,
                session_.fd(),
                "COMMANDS AUTH user pass | REALM_LIST | CHAR_LIST | CREATE_CHAR name race class gender | ENTER_WORLD name | STATE | MOVE dir amt | TARGET id | TAB_TARGET | ATTACK | CAST spell | EQUIP itemId | UNEQUIP slot | SAY text | WHO | QUEST_LIST | QUEST_ACCEPT id | QUEST_TURNIN id | VENDOR_LIST | BUY itemId | GOSSIP | GOSSIP_SELECT index | LOOT targetId | TELEPORT mapId | PING | HELP | QUIT\n"
            });
            continue;
        }
        if (opcode == WowOpcode::Quit) {
            if (session_.inWorld()) {
                actions.serviceMessages.push_back(makeMapLeaveRequest());
            } else {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "BYE\n"});
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, session_.fd(), {}});
                actions.eraseSession = true;
                session_.close();
            }
            continue;
        }

        if (!session_.authenticated()) {
            if (opcode != WowOpcode::Auth) {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR auth_required\n"});
                continue;
            }
            if (words.size() < 3) {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR usage AUTH user pass\n"});
                continue;
            }
            if (session_.loginPending()) {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR login_pending\n"});
                continue;
            }
            session_.beginLoginRequest();
            actions.serviceMessages.push_back(makeLoginRequest(words[1], words[2]));
            continue;
        }

        if (!session_.inWorld()) {
            if (session_.enteringWorld()) {
                session_.queuePendingCommand(std::move(line));
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "QUEUED enter_world_pending\n"});
                continue;
            }
            if (opcode == WowOpcode::RealmList) {
                actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), realmListMessage()});
                continue;
            }
            if (opcode == WowOpcode::CharList) {
                actions.serviceMessages.push_back(makeCharacterEnumRequest());
                continue;
            }
            if (opcode == WowOpcode::CreateChar) {
                if (words.size() < 5) {
                    actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR usage CREATE_CHAR name race class gender\n"});
                    continue;
                }
                actions.serviceMessages.push_back(makeCharacterCreateRequest(words[1], words[3]));
                continue;
            }
            if (opcode == WowOpcode::EnterWorld) {
                if (words.size() < 2) {
                    actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR usage ENTER_WORLD name\n"});
                    continue;
                }
                std::string selectedCharacter;
                for (std::size_t i = 1; i < words.size(); ++i) {
                    if (i > 1) {
                        selectedCharacter += ' ';
                    }
                    selectedCharacter += words[i];
                }
                session_.selectCharacter(std::move(selectedCharacter));
                session_.beginEnterWorld();
                actions.serviceMessages.push_back(makeCharacterLoginRequest());
                continue;
            }
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR character_select_required\n"});
            continue;
        }

        SessionActions routed = routeInWorldCommand(std::move(line));
        actions.socketCommands.insert(actions.socketCommands.end(),
                                      std::make_move_iterator(routed.socketCommands.begin()),
                                      std::make_move_iterator(routed.socketCommands.end()));
        actions.serviceMessages.insert(actions.serviceMessages.end(),
                                       std::make_move_iterator(routed.serviceMessages.begin()),
                                       std::make_move_iterator(routed.serviceMessages.end()));
        actions.eraseSession = actions.eraseSession || routed.eraseSession;
    }

    return actions;
}

SessionActions WowSessionAgent::onDisconnect() noexcept {
    SessionActions actions;
    session_.close();
    if (session_.inWorld()) {
        actions.serviceMessages.push_back(makeMapLeaveRequest());
    } else {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, session_.fd(), {}});
        actions.eraseSession = true;
    }
    return actions;
}

SessionActions WowSessionAgent::onWorldResponse(const GameResponse& response) noexcept {
    SessionActions actions;
    if (!response.text.empty()) {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), response.text});
    }
    if (response.type == GameResponseType::LeaveAck || response.closeAfterSend) {
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, session_.fd(), {}});
        actions.eraseSession = true;
        session_.close();
        session_.finishLeaveWorld();
    }
    return actions;
}

SessionActions WowSessionAgent::onLoginResponse(const LoginMessage& response) noexcept {
    SessionActions actions;
    if (!response.success) {
        session_.finishLoginFailure();
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "AUTH_FAIL reason=" + response.reason + "\n"
        });
        return actions;
    }

    session_.finishLoginSuccess(response.username);
    actions.socketCommands.push_back(SocketCommand{
        SocketCommandType::Send,
        session_.fd(),
        "AUTH_OK user=" + session_.accountName() + "\n" + realmListMessage()
    });
    actions.serviceMessages.push_back(makeCharacterEnumRequest());
    return actions;
}

SessionActions WowSessionAgent::onHallResponse(const HallMessage& response) noexcept {
    (void)response;
    return {};
}

SessionActions WowSessionAgent::onWowRuntimeResponse(const WowRuntimeMessage& response) noexcept {
    SessionActions actions;

    switch (response.op) {
    case WowRuntimeOp::CharacterEnumResult:
        loadCharactersFromPayload(response.payload);
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), characterListMessage()});
        break;
    case WowRuntimeOp::CharacterCreateResult:
        loadCharactersFromPayload(response.payload);
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "CHAR_CREATE_OK name=" + response.characterName + "\n" + characterListMessage()
        });
        break;
    case WowRuntimeOp::CharacterLoginResult: {
        if (!response.success) {
            session_.setEnteringWorld(false);
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR " + response.reason + "\n"});
            break;
        }
        const std::string zone = parseField(response.payload, "zone");
        session_.setMapContext(response.mapId, response.instanceId, zone.empty() ? std::string("Northshire") : zone);
        actions.serviceMessages.push_back(makeMapEnterRequest(response.mapId, response.instanceId));
        break;
    }
    case WowRuntimeOp::MapEnterResult:
        if (!response.success) {
            session_.setEnteringWorld(false);
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR map_enter_failed\n"});
            break;
        }
        session_.finishEnterWorld(session_.playerId(), response.mapId, response.instanceId, session_.zoneName());
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "CHAR_ENTER player=" + std::to_string(session_.playerId()) +
            " name=" + session_.characterName() +
            " zone=" + session_.zoneName() +
            " instance=" + std::to_string(session_.instanceId()) + "\n" +
            response.payload
        });
        actions.serviceMessages.push_back(makeCombatInitRequest());
        break;
    case WowRuntimeOp::CombatInitActor:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "UNIT player=" + std::to_string(session_.playerId()) + " name=" + session_.characterName() +
            " race=Human class=Warrior gender=Nonbinary faction=Alliance level=12 hp=100 hpMax=100 power=100 powerMax=100 x=48.0 y=48.0 z=0.0 o=0.0 hostile=0 dead=0 self=1\n"
            "POSITION player=" + std::to_string(session_.playerId()) + " x=48.0 y=48.0 z=0.0 o=0.0\n"
            "UNIT player=9001 name=Northshire_Wolf race=Wolf class=Beast gender=None faction=Neutral level=4 hp=72 hpMax=72 power=0 powerMax=0 x=60.0 y=52.0 z=0.0 o=0.0 hostile=1 dead=0 self=0 selected=0\n"
            "TARGET player=" + std::to_string(session_.playerId()) + " target=0 hostile=0 name=None\n"
            "COMBAT player=" + std::to_string(session_.playerId()) + " target=0 auto=0 dead=0\n"
        });
        actions.serviceMessages.push_back(makeMapSnapshotRequest());
        break;
    case WowRuntimeOp::MapSnapshotResult:
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), response.payload});
        for (std::string& pending : session_.drainPendingCommands()) {
            SessionActions routed = routeInWorldCommand(std::move(pending));
            actions.socketCommands.insert(actions.socketCommands.end(),
                                          std::make_move_iterator(routed.socketCommands.begin()),
                                          std::make_move_iterator(routed.socketCommands.end()));
            actions.serviceMessages.insert(actions.serviceMessages.end(),
                                           std::make_move_iterator(routed.serviceMessages.begin()),
                                           std::make_move_iterator(routed.serviceMessages.end()));
        }
        break;
    case WowRuntimeOp::CharacterWhoResult:
    case WowRuntimeOp::CharacterSpellbookResult:
    case WowRuntimeOp::CharacterQuestListResult:
    case WowRuntimeOp::CharacterQuestAcceptResult:
    case WowRuntimeOp::CharacterQuestTurnInResult:
    case WowRuntimeOp::CharacterVendorListResult:
    case WowRuntimeOp::CharacterGossipResult:
    case WowRuntimeOp::CharacterGossipSelectResult:
    case WowRuntimeOp::CharacterEquipResult:
    case WowRuntimeOp::CharacterUnequipResult:
    case WowRuntimeOp::CharacterTrainResult:
    case WowRuntimeOp::CharacterBuyResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            response.success ? response.payload : ("ERR " + response.reason + "\n")
        });
        break;
    case WowRuntimeOp::MapMoveResult:
        if (response.success) {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), response.payload + "MOVE_ACK player=" + std::to_string(session_.playerId()) + "\n"});
        }
        break;
    case WowRuntimeOp::MapTeleportResult:
        if (response.success) {
            const std::string zone = parseField(response.payload, "zone");
            session_.setMapContext(response.mapId, response.instanceId, zone.empty() ? std::string("Northshire") : zone);
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(),
                "TELEPORT player=" + std::to_string(session_.playerId()) +
                " map=" + std::to_string(response.mapId) +
                " zone=" + session_.zoneName() + "\n" + response.payload});
        } else {
            actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "ERR " + response.reason + "\n"});
        }
        break;
    case WowRuntimeOp::MapSayResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            response.success ? response.payload : ("ERR " + response.reason + "\n")
        });
        break;
    case WowRuntimeOp::CombatTargetResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "TARGET player=" + std::to_string(session_.playerId()) +
            " target=" + std::to_string(response.targetId) +
            " hostile=" + (response.value0 != 0 ? std::string("1") : std::string("0")) +
            " name=" + (response.characterName.empty() ? std::string("None") : response.characterName) + "\n" +
            response.payload
        });
        break;
    case WowRuntimeOp::CombatAttackResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            (response.success
                 ? "SWING source=" + std::to_string(session_.playerId()) + " target=" + std::to_string(response.targetId) + " amount=" + std::to_string(response.value0) + "\n"
                   "COMBAT player=" + std::to_string(session_.playerId()) + " target=" + std::to_string(response.targetId) + " auto=1 dead=0\n" +
                   (response.value1 != 0 ? "DEATH player=" + std::to_string(response.targetId) + " kind=Creature\n" : "") +
                   response.payload
                 : "ERR " + response.reason + "\n")
        });
        break;
    case WowRuntimeOp::CombatCastResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            (response.success
                ? "CHAT channel=SYSTEM from=Spell text=Cast_" + response.characterName + "\n"
                  "SWING source=" + std::to_string(session_.playerId()) + " target=" + std::to_string(response.targetId) + " amount=" + std::to_string(response.value0) + "\n" +
                  response.payload
                : "ERR " + response.reason + "\n")
        });
        break;
    case WowRuntimeOp::CombatLootResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            (response.success
                ? "LOOT target=" + std::to_string(response.targetId) + " gold=" + std::to_string(response.value0) + "\n" + response.payload
                : "ERR " + response.reason + "\n")
        });
        break;
    case WowRuntimeOp::CombatReleaseResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "RELEASE player=" + std::to_string(session_.playerId()) + "\nCOMBAT player=" + std::to_string(session_.playerId()) + " target=0 auto=0 dead=1\n"
        });
        break;
    case WowRuntimeOp::CombatResurrectResult:
        actions.socketCommands.push_back(SocketCommand{
            SocketCommandType::Send,
            session_.fd(),
            "RESURRECT player=" + std::to_string(session_.playerId()) + " by=SpiritHealer\nCOMBAT player=" + std::to_string(session_.playerId()) + " target=0 auto=0 dead=0\n"
        });
        break;
    case WowRuntimeOp::MapLeaveResult:
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Send, session_.fd(), "BYE\n"});
        actions.socketCommands.push_back(SocketCommand{SocketCommandType::Close, session_.fd(), {}});
        actions.eraseSession = true;
        session_.close();
        session_.finishLeaveWorld();
        break;
    default:
        break;
    }

    return actions;
}

SessionActions WowSessionAgent::routeInWorldCommand(std::string line) const {
    SessionActions actions;
    const std::vector<std::string> words = splitWords(line);
    if (words.empty()) {
        return actions;
    }

    if (words[0] == "STATE") {
        actions.serviceMessages.push_back(makeMapSnapshotRequest());
        return actions;
    }
    if (words[0] == "SAY" && words.size() >= 2) {
        const std::string prefix = "SAY ";
        actions.serviceMessages.push_back(makeMapSayRequest(line.size() > prefix.size() ? line.substr(prefix.size()) : std::string{}));
        return actions;
    }
    if (words[0] == "SPELLBOOK") {
        actions.serviceMessages.push_back(makeCharacterSpellbookRequest());
        return actions;
    }
    if (words[0] == "WHO") {
        actions.serviceMessages.push_back(makeCharacterWhoRequest());
        return actions;
    }
    if (words[0] == "QUEST_LIST") {
        actions.serviceMessages.push_back(makeCharacterQuestListRequest());
        return actions;
    }
    if (words[0] == "QUEST_ACCEPT" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterQuestAcceptRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "QUEST_TURNIN" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterQuestTurnInRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "VENDOR_LIST") {
        actions.serviceMessages.push_back(makeCharacterVendorListRequest());
        return actions;
    }
    if (words[0] == "GOSSIP") {
        actions.serviceMessages.push_back(makeCharacterGossipRequest());
        return actions;
    }
    if (words[0] == "GOSSIP_SELECT" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterGossipSelectRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "BUY" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterBuyRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "EQUIP" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterEquipRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "UNEQUIP" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterUnequipRequest(words[1]));
        return actions;
    }
    if (words[0] == "TRAIN" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCharacterTrainRequest(words[1]));
        return actions;
    }
    if (words[0] == "MOVE" && words.size() >= 2) {
        int dx = 0;
        int dy = 0;
        const std::string& direction = words[1];
        if (direction == "W" || direction == "N") dy = -2;
        else if (direction == "S") dy = 2;
        else if (direction == "A") dx = -2;
        else if (direction == "D" || direction == "E") dx = 2;
        actions.serviceMessages.push_back(makeMapMoveRequest(dx, dy));
        return actions;
    }
    if (words[0] == "TARGET" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCombatTargetRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "TAB_TARGET") {
        actions.serviceMessages.push_back(makeCombatTargetRequest(9001));
        return actions;
    }
    if (words[0] == "ATTACK") {
        actions.serviceMessages.push_back(makeCombatAttackRequest());
        return actions;
    }
    if (words[0] == "CAST" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCombatCastRequest(words[1]));
        return actions;
    }
    if (words[0] == "LOOT" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeCombatLootRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "RELEASE") {
        actions.serviceMessages.push_back(makeCombatReleaseRequest());
        return actions;
    }
    if (words[0] == "TELEPORT" && words.size() >= 2) {
        actions.serviceMessages.push_back(makeMapTeleportRequest(std::atoi(words[1].c_str())));
        return actions;
    }
    if (words[0] == "RESURRECT") {
        actions.serviceMessages.push_back(makeCombatResurrectRequest());
        return actions;
    }

    actions.serviceMessages.push_back(makeWorldCommand(std::move(line)));
    return actions;
}

std::string WowSessionAgent::trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

std::vector<std::string> WowSessionAgent::splitWords(std::string_view line) {
    std::istringstream in{std::string(line)};
    std::vector<std::string> words;
    std::string word;
    while (in >> word) {
        words.push_back(std::move(word));
    }
    return words;
}

std::string WowSessionAgent::parseField(std::string_view payload, std::string_view key) {
    std::istringstream in{std::string(payload)};
    std::string token;
    while (in >> token) {
        const std::size_t pos = token.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        if (token.substr(0, pos) == key) {
            return token.substr(pos + 1);
        }
    }
    return {};
}

void WowSessionAgent::loadCharactersFromPayload(std::string_view payload) {
    session_.clearCharacters();
    std::istringstream in{std::string(payload)};
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("name=", 0) != 0) {
            continue;
        }
        session_.addCharacter({
            parseField(line, "name"),
            parseField(line, "race"),
            parseField(line, "class"),
            parseField(line, "gender"),
            [&] {
                const std::string level = parseField(line, "level");
                return level.empty() ? 1 : std::atoi(level.c_str());
            }(),
            parseField(line, "zone")
        });
    }
}

std::string WowSessionAgent::realmListMessage() const {
    return "REALM_LIST count=1 selected=LocalDev\n"
           "REALM name=LocalDev address=127.0.0.1 population=low characters=" + std::to_string(session_.characters().size()) + "\n";
}

std::string WowSessionAgent::characterListMessage() const {
    std::ostringstream out;
    out << "CHAR_LIST count=" << session_.characters().size()
        << " account=" << (session_.accountName().empty() ? std::string("player") : session_.accountName()) << "\n";
    for (const auto& character : session_.characters()) {
        out << "CHAR name=" << character.name
            << " race=" << character.race
            << " class=" << character.klass
            << " gender=" << character.gender
            << " level=" << character.level
            << " zone=" << character.zone << "\n";
    }
    return out.str();
}

SkynetMessage WowSessionAgent::makeJoinMessage() const {
    GameCommand command;
    command.type = GameCommandType::Join;
    command.fd = session_.fd();
    command.playerId = session_.playerId();
    command.roomId = 1;
    command.line = session_.characterName();
    return wrapWorld(std::move(command));
}

SkynetMessage WowSessionAgent::makeLeaveMessage() const {
    return wrapWorld(GameCommand{GameCommandType::Leave, session_.fd(), session_.playerId(), 1, {}});
}

SkynetMessage WowSessionAgent::makeWorldCommand(std::string line) const {
    return wrapWorld(GameCommand{GameCommandType::Command, session_.fd(), session_.playerId(), 1, std::move(line)});
}

SkynetMessage WowSessionAgent::makeLoginRequest(std::string username, std::string password) const {
    LoginMessage login;
    login.type = LoginMessageType::Request;
    login.fd = session_.fd();
    login.username = std::move(username);
    login.password = std::move(password);
    return wrapLogin(std::move(login));
}

SkynetMessage WowSessionAgent::makeCharacterEnumRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterEnumRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.accountName = session_.accountName();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterCreateRequest(std::string characterName, std::string klass) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterCreateRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.accountName = session_.accountName();
    runtime.characterName = std::move(characterName);
    runtime.payload = std::move(klass);
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterLoginRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterLoginBegin;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.accountName = session_.accountName();
    runtime.characterName = session_.characterName();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterWhoRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterWhoRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.mapId = session_.mapId();
    runtime.instanceId = session_.instanceId();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterSpellbookRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterSpellbookRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterQuestListRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterQuestListRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterQuestAcceptRequest(int questId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterQuestAcceptRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.value0 = questId;
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterQuestTurnInRequest(int questId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterQuestTurnInRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.value0 = questId;
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterVendorListRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterVendorListRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterGossipRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterGossipRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterGossipSelectRequest(int index) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterGossipSelectRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.value0 = index;
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterEquipRequest(int itemId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterEquipRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.value0 = itemId;
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterUnequipRequest(std::string slot) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterUnequipRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.payload = std::move(slot);
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterTrainRequest(std::string spell) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterTrainRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.payload = std::move(spell);
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCharacterBuyRequest(int itemId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CharacterBuyRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.value0 = itemId;
    return wrapWowRuntime(ServiceId::WowCharacter, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeMapEnterRequest(int mapId, int instanceId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::MapEnterRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.characterName = session_.characterName();
    runtime.mapId = mapId;
    runtime.instanceId = instanceId;
    return wrapWowRuntime(ServiceId::WowMapInstance, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeMapLeaveRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::MapLeaveRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.mapId = session_.mapId();
    runtime.instanceId = session_.instanceId();
    return wrapWowRuntime(ServiceId::WowMapInstance, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeMapSnapshotRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::MapSnapshotRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.mapId = session_.mapId();
    runtime.instanceId = session_.instanceId();
    return wrapWowRuntime(ServiceId::WowMapInstance, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeMapMoveRequest(int dx, int dy) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::MapMoveRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.mapId = session_.mapId();
    runtime.instanceId = session_.instanceId();
    runtime.value0 = dx;
    runtime.value1 = dy;
    return wrapWowRuntime(ServiceId::WowMapInstance, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeMapTeleportRequest(int mapId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::MapTeleportRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.mapId = mapId;
    runtime.instanceId = session_.instanceId();
    return wrapWowRuntime(ServiceId::WowMapInstance, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeMapSayRequest(std::string text) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::MapSayRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.characterName = session_.characterName();
    runtime.payload = std::move(text);
    return wrapWowRuntime(ServiceId::WowMapInstance, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatInitRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatInitActor;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatTargetRequest(int targetId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatTargetRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.targetId = targetId;
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatAttackRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatAttackRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatCastRequest(std::string spellName) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatCastRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.characterName = std::move(spellName);
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatLootRequest(int targetId) const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatLootRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    runtime.targetId = targetId;
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatReleaseRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatReleaseRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

SkynetMessage WowSessionAgent::makeCombatResurrectRequest() const {
    WowRuntimeMessage runtime;
    runtime.op = WowRuntimeOp::CombatResurrectRequest;
    runtime.fd = session_.fd();
    runtime.playerId = session_.playerId();
    return wrapWowRuntime(ServiceId::WowCombat, std::move(runtime));
}

} // namespace wow
