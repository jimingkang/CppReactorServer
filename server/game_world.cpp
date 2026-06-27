#include "game_world.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace {

std::string firstToken(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    return command;
}

} // namespace

void PlatformGameWorld::onJoin(int playerId) {
    Player player;
    player.id = playerId;
    players_.emplace(playerId, player);
}

std::string PlatformGameWorld::onLeave(int playerId) {
    if (auto it = players_.find(playerId); it != players_.end()) {
        savedPlayers_[playerId] = it->second;
        players_.erase(it);
    }
    return {};
}

bool PlatformGameWorld::canHandle(const std::string& command) const {
    return command == "MOVE" || command == "POS" || command == "ATTACK" || command == "STATE" || command == "COIN";
}

std::string PlatformGameWorld::handleCommand(int playerId, const std::string& commandLine) {
    Player* player = find(playerId);
    if (player == nullptr) {
        return "ERR player_not_found\n";
    }

    std::istringstream input(commandLine);
    std::string command;
    input >> command;

    if (command == "MOVE") {
        int dx = 0;
        int dy = 0;
        input >> dx >> dy;
        player->x = std::clamp(player->x + dx, -100, 100);
        player->y = std::clamp(player->y + dy, -100, 100);
        player->score += std::max(1, std::abs(dx) + std::abs(dy));
        return "OK MOVE player=" + std::to_string(playerId) + " x=" + std::to_string(player->x) +
               " y=" + std::to_string(player->y) + " score=" + std::to_string(player->score) + "\n";
    }

    if (command == "POS") {
        int x = 0;
        int y = 0;
        input >> x >> y;
        player->x = std::clamp(x, 0, 2400);
        player->y = std::clamp(y, 0, 720);
        return "OK POS player=" + std::to_string(playerId) + " x=" + std::to_string(player->x) +
               " y=" + std::to_string(player->y) + " score=" + std::to_string(player->score) + "\n";
    }

    if (command == "COIN") {
        int coinId = -1;
        input >> coinId;
        auto it = std::find_if(coins_.begin(), coins_.end(), [coinId](const WorldCoin& coin) {
            return coin.id == coinId;
        });
        if (it == coins_.end()) {
            return "ERR coin_not_found\n";
        }
        if (it->collected) {
            return "ERR coin_already_collected\n";
        }
        it->collected = true;
        player->score += 10;
        return "OK COIN id=" + std::to_string(coinId) + " score=" + std::to_string(player->score) + "\n";
    }

    if (command == "ATTACK") {
        int targetId = 0;
        input >> targetId;
        Player* target = find(targetId);
        if (target == nullptr) {
            return "ERR target_not_found\n";
        }
        target->hp = std::max(0, target->hp - 10);
        player->score += 5;
        return "OK ATTACK target=" + std::to_string(targetId) + " hp=" + std::to_string(target->hp) + "\n";
    }

    if (command == "STATE") {
        return snapshot(playerId);
    }

    return "ERR platform_unknown_command\n";
}

std::string PlatformGameWorld::snapshot(int) const {
    std::ostringstream out;
    out << "STATE players=" << players_.size() << "\n";
    for (const auto& [id, player] : players_) {
        out << "PLAYER id=" << id << " x=" << player.x << " y=" << player.y
            << " hp=" << player.hp << " score=" << player.score << "\n";
    }
    out << "SAVED_PLAYERS count=" << savedPlayers_.size() << "\n";
    for (const auto& [id, player] : savedPlayers_) {
        out << "SAVED_PLAYER id=" << id << " x=" << player.x << " y=" << player.y
            << " hp=" << player.hp << " score=" << player.score << "\n";
    }
    out << "COINS count=" << coins_.size() << "\n";
    for (const WorldCoin& coin : coins_) {
        out << "COIN id=" << coin.id << " x=" << coin.x << " y=" << coin.y
            << " collected=" << (coin.collected ? 1 : 0) << "\n";
    }
    out << "MONSTERS count=" << monsters_.size() << "\n";
    for (const WorldMonster& monster : monsters_) {
        out << "MONSTER id=" << monster.id << " x=" << monster.x << " y=" << monster.y
            << " min=" << monster.minX << " max=" << monster.maxX << " speed=" << monster.speed << "\n";
    }
    return out.str();
}

Player* PlatformGameWorld::find(int playerId) {
    auto it = players_.find(playerId);
    return it == players_.end() ? nullptr : &it->second;
}

void TicTacToeWorld::onJoin(int playerId) {
    symbols_[playerId] = assignSymbol(playerId);
}

std::string TicTacToeWorld::onLeave(int playerId) {
    if (auto it = symbols_.find(playerId); it != symbols_.end()) {
        if (it->second == 'X') {
            xPlayerId_ = 0;
        } else if (it->second == 'O') {
            oPlayerId_ = 0;
        }
        symbols_.erase(it);
    }
    return {};
}

bool TicTacToeWorld::canHandle(const std::string& command) const {
    return command == "BOARD" || command == "CHESS" || command == "PLAY" || command == "RESET_BOARD";
}

std::string TicTacToeWorld::handleCommand(int playerId, const std::string& commandLine) {
    std::istringstream input(commandLine);
    std::string command;
    input >> command;

    if (command == "BOARD" || command == "CHESS") {
        return boardState(playerId);
    }

    if (command == "PLAY") {
        int cell = -1;
        input >> cell;
        return playMove(playerId, cell);
    }

    if (command == "RESET_BOARD") {
        resetBoard();
        return boardState(playerId);
    }

    return "ERR board_unknown_command\n";
}

std::string TicTacToeWorld::snapshot(int playerId) const {
    return boardState(playerId);
}

char TicTacToeWorld::assignSymbol(int playerId) {
    if (xPlayerId_ == 0) {
        xPlayerId_ = playerId;
        return 'X';
    }
    if (oPlayerId_ == 0) {
        oPlayerId_ = playerId;
        return 'O';
    }
    return '.';
}

std::string TicTacToeWorld::boardState(int playerId) const {
    char symbol = '.';
    if (auto it = symbols_.find(playerId); it != symbols_.end()) {
        symbol = it->second;
    }

    std::string cells;
    cells.reserve(board_.size());
    for (char cell : board_) {
        cells.push_back(cell);
    }

    std::ostringstream out;
    out << "BOARD player=" << playerId << " symbol=" << symbol << " next=" << nextTurn_
        << " winner=" << winner_ << " cells=" << cells << "\n";
    return out.str();
}

std::string TicTacToeWorld::playMove(int playerId, int cell) {
    const auto symbolIt = symbols_.find(playerId);
    if (symbolIt == symbols_.end()) {
        return "ERR player_not_found\n";
    }

    const char symbol = symbolIt->second;
    if (symbol != 'X' && symbol != 'O') {
        return "ERR board_full_observer\n" + boardState(playerId);
    }
    if (winner_ != '.') {
        return "ERR board_finished\n" + boardState(playerId);
    }
    if (symbol != nextTurn_) {
        return "ERR not_your_turn\n" + boardState(playerId);
    }
    if (cell < 0 || cell >= static_cast<int>(board_.size())) {
        return "ERR invalid_cell\n" + boardState(playerId);
    }
    if (board_[static_cast<std::size_t>(cell)] != '.') {
        return "ERR occupied_cell\n" + boardState(playerId);
    }

    board_[static_cast<std::size_t>(cell)] = symbol;
    winner_ = checkWinner();
    if (winner_ == '.') {
        const bool hasEmpty = std::any_of(board_.begin(), board_.end(), [](char value) { return value == '.'; });
        if (!hasEmpty) {
            winner_ = 'D';
        } else {
            nextTurn_ = nextTurn_ == 'X' ? 'O' : 'X';
        }
    }

    return "OK PLAY cell=" + std::to_string(cell) + "\n" + boardState(playerId);
}

void TicTacToeWorld::resetBoard() {
    board_.fill('.');
    nextTurn_ = 'X';
    winner_ = '.';
}

char TicTacToeWorld::checkWinner() const {
    constexpr int lines[8][3] = {
        {0, 1, 2}, {3, 4, 5}, {6, 7, 8},
        {0, 3, 6}, {1, 4, 7}, {2, 5, 8},
        {0, 4, 8}, {2, 4, 6},
    };
    for (const auto& line : lines) {
        const char a = board_[static_cast<std::size_t>(line[0])];
        if (a != '.' && a == board_[static_cast<std::size_t>(line[1])] && a == board_[static_cast<std::size_t>(line[2])]) {
            return a;
        }
    }
    return '.';
}

GameWorld::GameWorld() : games_{&platform_, &ticTacToe_} {}

int GameWorld::join() {
    const int id = nextPlayerId_++;
    for (IGameService* game : games_) {
        game->onJoin(id);
    }
    return id;
}

std::string GameWorld::leave(int playerId) {
    for (IGameService* game : games_) {
        game->onLeave(playerId);
    }
    return "BYE player=" + std::to_string(playerId) + "\n";
}

std::string GameWorld::handleCommand(int playerId, const std::string& commandLine) {
    const std::string command = commandName(commandLine);

    if (command == "PING") {
        return "PONG\n";
    }

    if (command == "HELP") {
        return "COMMANDS POS x y | MOVE dx dy | ATTACK playerId | COIN coinId | STATE | BOARD | PLAY 0-8 | RESET_BOARD | PING | QUIT\n";
    }

    if (command == "QUIT") {
        return "QUIT\n";
    }

    if (command == "STATE") {
        return snapshot();
    }

    for (IGameService* game : games_) {
        if (game->canHandle(command)) {
            return game->handleCommand(playerId, commandLine);
        }
    }

    return "ERR unknown_command. Try HELP\n";
}

std::string GameWorld::snapshot() const {
    return platform_.snapshot(0) + ticTacToe_.snapshot(0);
}

std::string GameWorld::commandName(const std::string& commandLine) {
    return firstToken(commandLine);
}
