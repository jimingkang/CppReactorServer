#pragma once

#include "../board_game_world.h"
#include "guess_number_room_manager.h"

#include <random>
#include <memory>
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
    struct GuessSecret {
        int value = 0;
        int attempts = 0;
    };

    GuessSecret generateSecret();
    PlayerInfo* getPlayer(int playerId);
    RoomInfo* getRoom(int roomId);
    std::string getRoomSnapshot(int roomId) const;
    void broadcastRoom(int roomId, std::string text);

    std::mt19937 rng_;
    std::unique_ptr<GuessNumberRoomManager> roomManager_;
    std::unordered_map<int, GuessSecret> roomSecrets_;
    std::vector<GameResponse> pendingResponses_;
};

} // namespace guessnumber
