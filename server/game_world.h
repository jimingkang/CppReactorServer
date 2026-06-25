#pragma once

#include <string>
#include <unordered_map>

struct Player {
    int id = 0;
    int x = 0;
    int y = 0;
    int hp = 100;
    int score = 0;
};

class GameWorld {
public:
    int join();
    std::string leave(int playerId);
    std::string handleCommand(int playerId, const std::string& commandLine);
    std::string snapshot() const;

private:
    Player* find(int playerId);

    int nextPlayerId_ = 1;
    std::unordered_map<int, Player> players_;
};
