#pragma once

#include "../board_game_world.h"

#include <random>
#include <unordered_map>
#include <vector>

namespace guessnumber {

class GuessNumberGameWorld final : public BoardGameWorld {
public:
    GuessNumberGameWorld();

    GameResponse join(const GameCommand& command) override;
    GameResponse leave(const GameCommand& command) override;
    GameResponse handleCommand(const GameCommand& command) override;
    std::string snapshot() const override;
    std::vector<GameResponse> takePendingResponses() override;

private:
    struct PlayerState {
        int playerId = 0;
        int fd = -1;
        int roomId = 0;
        int score = 0;
    };

    struct RoomState {
        int roomId = 0;
        int secret = 0;
        bool finished = false;
        int losingPlayerId = 0;
        std::vector<int> players;
        std::vector<std::string> history;
    };

    PlayerState* player(int playerId);
    RoomState* room(int roomId);
    std::string roomSnapshot(int roomId) const;
    void broadcastRoom(int roomId, std::string text);

    std::mt19937 rng_;
    std::unordered_map<int, PlayerState> players_;
    std::unordered_map<int, RoomState> rooms_;
    std::vector<GameResponse> pendingResponses_;
};

} // namespace guessnumber
