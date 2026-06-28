#pragma once

#include <cstdint>
#include <string>

enum class ServiceId : std::uint32_t {
    Logger = 1,
    Gate = 2,
    Connection = 3,
    GameWorld = 4,
    Room = 5,
    Db = 6,
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

enum class DbMessageType {
    SavePlayer,
};

struct DbMessage {
    DbMessageType type = DbMessageType::SavePlayer;
    int playerId = 0;
};

enum class MessageKind {
    Socket,
    GameCommand,
    GameResponse,
    Log,
    Room,
    Db,
};

struct SkynetMessage {
    ServiceId source = ServiceId::Gate;
    ServiceId destination = ServiceId::Gate;
    int session = 0;
    MessageKind kind = MessageKind::Socket;
    SocketMessage socket;
    GameCommand gameCommand;
    GameResponse gameResponse;
    RoomMessage room;
    DbMessage db;
    std::string text;
};
