#pragma once

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Player {
    int id = 0;
    int x = 0;
    int y = 0;
    int hp = 100;
    int score = 0;
};

struct WorldCoin {
    int id = 0;
    int x = 0;
    int y = 0;
    bool collected = false;
};

struct WorldMonster {
    int id = 0;
    int x = 0;
    int y = 0;
    int minX = 0;
    int maxX = 0;
    int speed = 0;
};

class IGameService {
public:
    virtual ~IGameService() = default;

    virtual void onJoin(int playerId) = 0;
    virtual std::string onLeave(int playerId) = 0;
    virtual bool canHandle(const std::string& command) const = 0;
    virtual std::string handleCommand(int playerId, const std::string& commandLine) = 0;
    virtual std::string snapshot(int playerId) const = 0;
};

class PlatformGameWorld final : public IGameService {
public:
    void onJoin(int playerId) override;
    std::string onLeave(int playerId) override;
    bool canHandle(const std::string& command) const override;
    std::string handleCommand(int playerId, const std::string& commandLine) override;
    std::string snapshot(int playerId) const override;

private:
    Player* find(int playerId);

    std::unordered_map<int, Player> players_;
    std::vector<WorldCoin> coins_ = {
        {0, 315, 305}, {1, 370, 305}, {2, 600, 265}, {3, 665, 265},
        {4, 910, 325}, {5, 1240, 280}, {6, 1310, 280}, {7, 1585, 320},
        {8, 1660, 320}, {9, 1905, 270}, {10, 1985, 270}, {11, 2230, 392},
    };
    std::vector<WorldMonster> monsters_ = {
        {0, 720, 402, 690, 820, 70},
        {1, 1060, 402, 1040, 1160, -65},
        {2, 1740, 327, 1730, 1840, 55},
        {3, 2100, 402, 2040, 2200, -80},
    };
    std::unordered_map<int, Player> savedPlayers_;
};

class TicTacToeWorld final : public IGameService {
public:
    void onJoin(int playerId) override;
    std::string onLeave(int playerId) override;
    bool canHandle(const std::string& command) const override;
    std::string handleCommand(int playerId, const std::string& commandLine) override;
    std::string snapshot(int playerId) const override;

private:
    char assignSymbol(int playerId);
    std::string boardState(int playerId) const;
    std::string playMove(int playerId, int cell);
    void resetBoard();
    char checkWinner() const;

    std::unordered_map<int, char> symbols_;
    std::array<char, 9> board_{'.', '.', '.', '.', '.', '.', '.', '.', '.'};
    int xPlayerId_ = 0;
    int oPlayerId_ = 0;
    char nextTurn_ = 'X';
    char winner_ = '.';
};

class GameWorld {
public:
    GameWorld();

    int join();
    std::string leave(int playerId);
    std::string handleCommand(int playerId, const std::string& commandLine);
    std::string snapshot() const;

private:
    static std::string commandName(const std::string& commandLine);

    int nextPlayerId_ = 1;
    PlatformGameWorld platform_;
    TicTacToeWorld ticTacToe_;
    std::vector<IGameService*> games_;
};
