#pragma once

#include <string>
#include <unordered_map>
#include <deque>
#include <vector>
#include <memory>

namespace guessnumber {

// 玩家等级划分（基于积分）
enum class PlayerLevel {
    Beginner,      // 0-100
    Intermediate,  // 101-300
    Advanced,      // 301-500
    Master,        // 500+
};

PlayerLevel getPlayerLevel(int score);

struct WaitingPlayer {
    int playerId = 0;
    int fd = -1;
    int score = 0;
    PlayerLevel level = PlayerLevel::Beginner;
    std::string username;
};

struct MatchQueue {
    PlayerLevel level;
    std::deque<WaitingPlayer> players;
    int minSeats = 2;
    int maxSeats = 4;
};

class GuessNumberHallManager {
public:
    GuessNumberHallManager();
    
    // 加入匹配队列
    bool joinQueue(int playerId, int fd, int score, std::string username, int seatCount = 3);
    
    // 离开匹配队列
    bool leaveQueue(int playerId);
    
    // 查询玩家在队列中的位置
    int getQueuePosition(int playerId);
    
    // 检查是否应该创建房间（返回应创建的房间ID和玩家列表）
    struct MatchResult {
        int roomId = 0;
        std::vector<WaitingPlayer> players;
    };
    MatchResult tryBuildMatch(int nextRoomId);
    
    // 查询队列统计
    struct QueueStats {
        int totalWaiting = 0;
        int beginnerCount = 0;
        int intermediateCount = 0;
        int advancedCount = 0;
        int masterCount = 0;
    };
    QueueStats getQueueStats() const;
    
    // 查询玩家等级
    PlayerLevel getPlayerLevel(int playerId) const;
    
    // 获取玩家等级对应的队列
    MatchQueue* getQueueForLevel(PlayerLevel level);
    
    // 清理队列
    void clearQueues();
    
    // 生成快照
    std::string queueSnapshot() const;
    
private:
    std::unordered_map<PlayerLevel, MatchQueue> queues_;
    std::unordered_map<int, PlayerLevel> playerLevels_;  // 快速查找玩家等级
};

} // namespace guessnumber
