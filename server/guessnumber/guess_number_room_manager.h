#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <chrono>

namespace guessnumber {

enum class RoomState {
    Waiting,    // 等待玩家加入
    Playing,    // 游戏中
    Finished,   // 游戏结束
};

struct RoomInfo {
    int roomId = 0;
    int secret = 0;
    RoomState state = RoomState::Waiting;
    int losingPlayerId = 0;
    std::vector<int> playerIds;
    std::vector<std::string> guessHistory;
    std::chrono::steady_clock::time_point createdAt;
    std::chrono::steady_clock::time_point finishedAt;
};

struct PlayerInfo {
    int playerId = 0;
    int fd = -1;
    int currentRoomId = 0;
    int score = 0;
    int gamesPlayed = 0;
    int gamesWon = 0;
    bool disconnected = false;
    std::chrono::steady_clock::time_point lastSeen;
};

class GuessNumberRoomManager {
public:
    GuessNumberRoomManager();
    
    // Room 管理
    RoomInfo* createRoom(int roomId, int seatCount);
    RoomInfo* getRoom(int roomId);
    const RoomInfo* getRoom(int roomId) const;
    bool removeRoom(int roomId);
    
    // Player 管理
    PlayerInfo* createPlayer(int playerId, int fd);
    PlayerInfo* getPlayer(int playerId);
    const PlayerInfo* getPlayer(int playerId) const;
    bool removePlayer(int playerId);
    
    // Room 操作
    bool joinRoom(int roomId, int playerId);
    bool leaveRoom(int roomId, int playerId);
    bool isRoomFull(int roomId, int maxSeats) const;
    bool isRoomEmpty(int roomId) const;
    
    // Game 逻辑
    bool recordGuess(int roomId, int playerId, int guess);
    void finishRoom(int roomId, int losingPlayerId);
    
    // Query
    std::string getRoomSummary(int roomId) const;
    std::string getPlayerStats(int playerId) const;
    std::string getGlobalStats() const;
    std::vector<RoomInfo*> getAllRooms();
    std::vector<PlayerInfo*> getAllPlayers();
    
    // Cleanup
    void removeExpiredRooms(int timeoutSeconds);
    
    // Reconnection support
    PlayerInfo* findDisconnectedPlayer(int playerId);
    void markPlayerDisconnected(int playerId);
    void reconnectPlayer(int playerId, int newFd);
    
private:
    std::unordered_map<int, RoomInfo> rooms_;
    std::unordered_map<int, PlayerInfo> players_;
    int nextRoomId_ = 1000;
};

} // namespace guessnumber
