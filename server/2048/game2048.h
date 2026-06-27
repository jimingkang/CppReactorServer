#pragma once

#include "../board_game_world.h"

#include <array>
#include <random>
#include <string>
#include <unordered_map>

class Game2048 final : public BoardGameWorld {
public:
    Game2048();

    int join() override;
    std::string leave(int playerId) override;
    std::string handleCommand(int playerId, const std::string& line) override;
    std::string snapshot() const override;

private:
    struct Board {
        std::array<int, 16> cells{};
        int score = 0;
        int best = 0;
        bool over = false;
        bool won = false;
    };

    Board& boardFor(int playerId);
    void reset(Board& board);
    bool move(Board& board, const std::string& direction);
    static bool moveLine(std::array<int, 4>& line, int& gained);
    static bool canMove(const Board& board);
    void addRandomTile(Board& board);
    std::string stateLine(int playerId, const Board& board) const;
    std::string playerSnapshot(int playerId) const;

    int nextPlayerId_ = 1;
    std::unordered_map<int, Board> boards_;
    std::mt19937 rng_;
};
