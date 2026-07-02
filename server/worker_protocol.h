#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class ServiceId : std::uint32_t {
    Logger = 1,
    Gate = 2,
    Connection = 3,
    GameWorld = 4,
    Room = 5,
    Login = 6,
    Db = 7,
    Hall = 8,
    Redis = 9,
    WowCharacter = 10,
    WowMapInstance = 11,
    WowCombat = 12,
};

enum class SocketMessageType {
    Accept,
    Data,
    Close,
    Error,
};

struct SocketMessage {
    SocketMessageType type = SocketMessageType::Data;
    int fd = -1;
    std::string data;
};

enum class GameCommandType {
    Join,
    Command,
    Leave,
};

struct GameCommand {
    GameCommandType type = GameCommandType::Command;
    int fd = -1;
    int playerId = 0;
    int roomId = 0;
    std::string line;
};

enum class GameResponseType {
    Joined,
    Text,
    LeaveAck,
};

struct GameResponse {
    GameResponseType type = GameResponseType::Text;
    int fd = -1;
    int playerId = 0;
    int roomId = 0;
    std::string text;
    bool closeAfterSend = false;
};

enum class RoomMessageType {
    PlayerJoined,
    PlayerLeft,
};

struct RoomMessage {
    RoomMessageType type = RoomMessageType::PlayerJoined;
    int playerId = 0;
};

enum class HallMessageType {
    ListRooms,
    CreateRoom,
    AutoMatch,
    CancelMatch,
    JoinRoom,
    LeaveRoom,
    Result,
};

struct HallMessage {
    HallMessageType type = HallMessageType::ListRooms;
    int fd = -1;
    int playerId = 0;
    int roomId = 0;
    int seatCount = 2;
    bool success = false;
    std::string gameType;
    std::string roomName;
    std::string reason;
    std::string payload;
    std::vector<int> playerIds;
};

enum class DbMessageType {
    SavePlayer,
    CheckCredentials,
    CredentialsResult,
};

struct DbMessage {
    DbMessageType type = DbMessageType::SavePlayer;
    int playerId = 0;
    int fd = -1;
    std::string username;
    std::string password;
    bool success = false;
    std::string reason;
};

enum class RedisMessageType {
    Get,
    Set,
    Delete,
    KeysByPrefix,
    Result,
};

struct RedisMessage {
    RedisMessageType type = RedisMessageType::Get;
    std::string key;
    std::string value;
    std::string prefix;
    bool success = false;
    std::string reason;
    std::vector<std::string> values;
};

enum class WowRuntimeOp {
    CharacterEnumRequest,
    CharacterEnumResult,
    CharacterCreateRequest,
    CharacterCreateResult,
    CharacterDeleteRequest,
    CharacterDeleteResult,
    CharacterLoginBegin,
    CharacterLoginResult,
    CharacterWhoRequest,
    CharacterWhoResult,
    CharacterSpellbookRequest,
    CharacterSpellbookResult,
    CharacterQuestListRequest,
    CharacterQuestListResult,
    CharacterQuestAcceptRequest,
    CharacterQuestAcceptResult,
    CharacterQuestTurnInRequest,
    CharacterQuestTurnInResult,
    CharacterVendorListRequest,
    CharacterVendorListResult,
    CharacterGossipRequest,
    CharacterGossipResult,
    CharacterGossipSelectRequest,
    CharacterGossipSelectResult,
    CharacterEquipRequest,
    CharacterEquipResult,
    CharacterUnequipRequest,
    CharacterUnequipResult,
    CharacterTrainRequest,
    CharacterTrainResult,
    CharacterBuyRequest,
    CharacterBuyResult,
    MapEnterRequest,
    MapEnterResult,
    MapLeaveRequest,
    MapLeaveResult,
    MapSnapshotRequest,
    MapSnapshotResult,
    MapMoveRequest,
    MapMoveResult,
    MapTeleportRequest,
    MapTeleportResult,
    MapSayRequest,
    MapSayResult,
    MapQueryUnitRequest,
    MapQueryUnitResult,
    MapDamageUnitRequest,
    MapDamageUnitResult,
    MapLootUnitRequest,
    MapLootUnitResult,
    CombatInitActor,
    CombatTargetRequest,
    CombatTargetResult,
    CombatAttackRequest,
    CombatAttackResult,
    CombatCastRequest,
    CombatCastResult,
    CombatLootRequest,
    CombatLootResult,
    CombatReleaseRequest,
    CombatReleaseResult,
    CombatResurrectRequest,
    CombatResurrectResult,
    Tick,
};

struct WowRuntimeMessage {
    WowRuntimeOp op = WowRuntimeOp::CharacterEnumRequest;
    int fd = -1;
    int playerId = 0;
    int mapId = 0;
    int instanceId = 0;
    int targetId = 0;
    int value0 = 0;
    int value1 = 0;
    bool success = false;
    std::string accountName;
    std::string characterName;
    std::string payload;
    std::string reason;
};

enum class LoginMessageType {
    Request,
    Result,
};

struct LoginMessage {
    LoginMessageType type = LoginMessageType::Request;
    int fd = -1;
    std::string username;
    std::string password;
    bool success = false;
    std::string reason;
};

enum class SocketCommandType {
    Send,
    Close,
};

struct SocketCommand {
    SocketCommandType type = SocketCommandType::Send;
    int fd = -1;
    std::string data;
};

enum class MessageKind {
    Socket,
    GameCommand,
    GameResponse,
    Login,
    Log,
    Room,
    Db,
    Hall,
    Redis,
    WowRuntime,
};

struct SkynetMessage {
    ServiceId source = ServiceId::Gate;
    ServiceId destination = ServiceId::Gate;
    int session = 0;
    std::uint64_t requestId = 0;
    std::uint64_t replyTo = 0;
    MessageKind kind = MessageKind::Socket;
    SocketMessage socket;
    GameCommand gameCommand;
    GameResponse gameResponse;
    LoginMessage login;
    RoomMessage room;
    DbMessage db;
    HallMessage hall;
    RedisMessage redis;
    WowRuntimeMessage wowRuntime;
    std::string text;
};
