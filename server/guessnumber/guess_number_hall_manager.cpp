#include "guess_number_hall_manager.h"

#include <algorithm>
#include <sstream>

namespace guessnumber {

PlayerLevel getPlayerLevel(int score) {
    if (score <= 100) return PlayerLevel::Beginner;
    if (score <= 300) return PlayerLevel::Intermediate;
    if (score <= 500) return PlayerLevel::Advanced;
    return PlayerLevel::Master;
}

GuessNumberHallManager::GuessNumberHallManager() {
    queues_[PlayerLevel::Beginner] = MatchQueue{PlayerLevel::Beginner, {}, 2, 4};
    queues_[PlayerLevel::Intermediate] = MatchQueue{PlayerLevel::Intermediate, {}, 2, 4};
    queues_[PlayerLevel::Advanced] = MatchQueue{PlayerLevel::Advanced, {}, 2, 4};
    queues_[PlayerLevel::Master] = MatchQueue{PlayerLevel::Master, {}, 2, 4};
}

bool GuessNumberHallManager::joinQueue(int playerId, int fd, int score, std::string username, int seatCount) {
    PlayerLevel level = getPlayerLevel(score);
    
    auto it = queues_.find(level);
    if (it == queues_.end()) {
        return false;
    }
    
    // Check if player already in queue
    for (const auto& player : it->second.players) {
        if (player.playerId == playerId) {
            return true;  // Already in queue
        }
    }
    
    WaitingPlayer wp;
    wp.playerId = playerId;
    wp.fd = fd;
    wp.score = score;
    wp.level = level;
    wp.username = std::move(username);
    
    it->second.players.push_back(wp);
    playerLevels_[playerId] = level;
    
    // Update seat count if different
    if (seatCount >= 2 && seatCount <= 4) {
        it->second.maxSeats = seatCount;
    }
    
    return true;
}

bool GuessNumberHallManager::leaveQueue(int playerId) {
    for (auto& [level, queue] : queues_) {
        auto it = std::find_if(queue.players.begin(), queue.players.end(),
                               [playerId](const WaitingPlayer& p) { return p.playerId == playerId; });
        if (it != queue.players.end()) {
            queue.players.erase(it);
            playerLevels_.erase(playerId);
            return true;
        }
    }
    return false;
}

int GuessNumberHallManager::getQueuePosition(int playerId) {
    for (auto& [level, queue] : queues_) {
        for (std::size_t i = 0; i < queue.players.size(); ++i) {
            if (queue.players[i].playerId == playerId) {
                return static_cast<int>(i) + 1;
            }
        }
    }
    return -1;
}

GuessNumberHallManager::MatchResult GuessNumberHallManager::tryBuildMatch(int nextRoomId) {
    MatchResult result;
    
    // Try to build match from each level, prioritize higher levels
    std::vector<PlayerLevel> levels = {
        PlayerLevel::Master,
        PlayerLevel::Advanced,
        PlayerLevel::Intermediate,
        PlayerLevel::Beginner
    };
    
    for (PlayerLevel level : levels) {
        auto it = queues_.find(level);
        if (it == queues_.end()) {
            continue;
        }
        
        MatchQueue& queue = it->second;
        if (static_cast<int>(queue.players.size()) >= queue.maxSeats) {
            // Build match
            result.roomId = nextRoomId;
            for (int i = 0; i < queue.maxSeats; ++i) {
                result.players.push_back(queue.players.front());
                queue.players.pop_front();
                playerLevels_.erase(result.players.back().playerId);
            }
            return result;
        }
    }
    
    return result;
}

GuessNumberHallManager::QueueStats GuessNumberHallManager::getQueueStats() const {
    QueueStats stats;
    
    for (const auto& [level, queue] : queues_) {
        stats.totalWaiting += queue.players.size();
        switch (level) {
            case PlayerLevel::Beginner:
                stats.beginnerCount = queue.players.size();
                break;
            case PlayerLevel::Intermediate:
                stats.intermediateCount = queue.players.size();
                break;
            case PlayerLevel::Advanced:
                stats.advancedCount = queue.players.size();
                break;
            case PlayerLevel::Master:
                stats.masterCount = queue.players.size();
                break;
        }
    }
    
    return stats;
}

PlayerLevel GuessNumberHallManager::getPlayerLevel(int playerId) const {
    auto it = playerLevels_.find(playerId);
    return it == playerLevels_.end() ? PlayerLevel::Beginner : it->second;
}

MatchQueue* GuessNumberHallManager::getQueueForLevel(PlayerLevel level) {
    auto it = queues_.find(level);
    return it == queues_.end() ? nullptr : &it->second;
}

void GuessNumberHallManager::clearQueues() {
    for (auto& [_, queue] : queues_) {
        queue.players.clear();
    }
    playerLevels_.clear();
}

std::string GuessNumberHallManager::queueSnapshot() const {
    std::ostringstream out;
    
    auto stats = getQueueStats();
    out << "QUEUE_STATS total=" << stats.totalWaiting
        << " beginner=" << stats.beginnerCount
        << " intermediate=" << stats.intermediateCount
        << " advanced=" << stats.advancedCount
        << " master=" << stats.masterCount << "\n";
    
    for (const auto& [level, queue] : queues_) {
        const char* levelName = "";
        switch (level) {
            case PlayerLevel::Beginner: levelName = "beginner"; break;
            case PlayerLevel::Intermediate: levelName = "intermediate"; break;
            case PlayerLevel::Advanced: levelName = "advanced"; break;
            case PlayerLevel::Master: levelName = "master"; break;
        }
        
        out << "LEVEL " << levelName << " count=" << queue.players.size() << "\n";
        for (std::size_t i = 0; i < queue.players.size(); ++i) {
            const auto& p = queue.players[i];
            out << "  " << (i + 1) << ". player=" << p.playerId
                << " score=" << p.score
                << " username=" << p.username << "\n";
        }
    }
    
    return out.str();
}

} // namespace guessnumber
