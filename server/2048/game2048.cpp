#include "game2048.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>

namespace {

std::string firstToken(const std::string& line) {
    std::istringstream input(line);
    std::string command;
    input >> command;
    return command;
}

std::string upper(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return value;
}

} // namespace

Game2048::Game2048() : rng_(std::random_device{}()) {
}

int Game2048::join() {
    const int playerId = nextPlayerId_++;
    Board board;
    reset(board);
    boards_[playerId] = board;
    return playerId;
}

std::string Game2048::leave(int playerId) {
    boards_.erase(playerId);
    return "BYE player=" + std::to_string(playerId) + "\n";
}

std::string Game2048::handleCommand(int playerId, const std::string& line) {
    const std::string command = upper(firstToken(line));
    Board& board = boardFor(playerId);

    if (command == "HELP") {
        return "COMMANDS STATE | MOVE LEFT|RIGHT|UP|DOWN | RESET | QUIT\n";
    }
    if (command == "RESET") {
        reset(board);
        return "OK RESET\n";
    }
    if (command == "STATE") {
        return playerSnapshot(playerId);
    }
    if (command == "MOVE") {
        std::istringstream input(line);
        std::string ignored;
        std::string direction;
        input >> ignored >> direction;
        direction = upper(direction);
        if (direction != "LEFT" && direction != "RIGHT" && direction != "UP" && direction != "DOWN") {
            return "ERR invalid_direction\n";
        }
        if (board.over) {
            return "ERR game_over\n";
        }
        if (!move(board, direction)) {
            board.over = !canMove(board);
            return "ERR no_move\n";
        }
        addRandomTile(board);
        board.best = std::max(board.best, board.score);
        board.won = board.won || std::any_of(board.cells.begin(), board.cells.end(), [](int value) {
            return value >= 2048;
        });
        board.over = !canMove(board);
        return "OK MOVE direction=" + direction + "\n";
    }
    if (command == "QUIT") {
        return "QUIT\n";
    }

    return "ERR unknown_command\n";
}

std::string Game2048::snapshot() const {
    std::ostringstream out;
    out << "STATE players=" << boards_.size() << "\n";
    for (const auto& [playerId, board] : boards_) {
        out << stateLine(playerId, board);
    }
    return out.str();
}

std::string Game2048::playerSnapshot(int playerId) const {
    if (auto it = boards_.find(playerId); it != boards_.end()) {
        return stateLine(playerId, it->second);
    }
    return "STATE player=" + std::to_string(playerId) +
           " score=0 best=0 over=0 won=0 cells=0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n";
}

Game2048::Board& Game2048::boardFor(int playerId) {
    auto [it, inserted] = boards_.try_emplace(playerId);
    if (inserted) {
        reset(it->second);
    }
    return it->second;
}

void Game2048::reset(Board& board) {
    board.cells.fill(0);
    board.score = 0;
    board.over = false;
    board.won = false;
    addRandomTile(board);
    addRandomTile(board);
}

std::string Game2048::stateLine(int playerId, const Board& board) const {
    std::ostringstream out;
    out << "STATE player=" << playerId << " score=" << board.score << " best=" << board.best
        << " over=" << (board.over ? 1 : 0) << " won=" << (board.won ? 1 : 0) << " cells=";
    for (std::size_t i = 0; i < board.cells.size(); ++i) {
        if (i != 0) {
            out << ',';
        }
        out << board.cells[i];
    }
    out << "\n";
    return out.str();
}

bool Game2048::move(Board& board, const std::string& direction) {
    const auto before = board.cells;
    int gained = 0;

    for (int i = 0; i < 4; ++i) {
        std::array<int, 4> line{};
        for (int j = 0; j < 4; ++j) {
            if (direction == "LEFT") {
                line[static_cast<std::size_t>(j)] = board.cells[static_cast<std::size_t>(i * 4 + j)];
            } else if (direction == "RIGHT") {
                line[static_cast<std::size_t>(j)] = board.cells[static_cast<std::size_t>(i * 4 + (3 - j))];
            } else if (direction == "UP") {
                line[static_cast<std::size_t>(j)] = board.cells[static_cast<std::size_t>(j * 4 + i)];
            } else {
                line[static_cast<std::size_t>(j)] = board.cells[static_cast<std::size_t>((3 - j) * 4 + i)];
            }
        }

        moveLine(line, gained);

        for (int j = 0; j < 4; ++j) {
            if (direction == "LEFT") {
                board.cells[static_cast<std::size_t>(i * 4 + j)] = line[static_cast<std::size_t>(j)];
            } else if (direction == "RIGHT") {
                board.cells[static_cast<std::size_t>(i * 4 + (3 - j))] = line[static_cast<std::size_t>(j)];
            } else if (direction == "UP") {
                board.cells[static_cast<std::size_t>(j * 4 + i)] = line[static_cast<std::size_t>(j)];
            } else {
                board.cells[static_cast<std::size_t>((3 - j) * 4 + i)] = line[static_cast<std::size_t>(j)];
            }
        }
    }

    board.score += gained;
    return board.cells != before;
}

bool Game2048::moveLine(std::array<int, 4>& line, int& gained) {
    std::array<int, 4> compact{};
    std::size_t write = 0;
    for (int value : line) {
        if (value != 0) {
            compact[write++] = value;
        }
    }

    for (std::size_t i = 0; i + 1 < compact.size(); ++i) {
        if (compact[i] != 0 && compact[i] == compact[i + 1]) {
            compact[i] *= 2;
            gained += compact[i];
            compact[i + 1] = 0;
        }
    }

    std::array<int, 4> merged{};
    write = 0;
    for (int value : compact) {
        if (value != 0) {
            merged[write++] = value;
        }
    }

    const bool changed = line != merged;
    line = merged;
    return changed;
}

bool Game2048::canMove(const Board& board) {
    if (std::any_of(board.cells.begin(), board.cells.end(), [](int value) { return value == 0; })) {
        return true;
    }
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const int value = board.cells[static_cast<std::size_t>(y * 4 + x)];
            if (x < 3 && value == board.cells[static_cast<std::size_t>(y * 4 + x + 1)]) {
                return true;
            }
            if (y < 3 && value == board.cells[static_cast<std::size_t>((y + 1) * 4 + x)]) {
                return true;
            }
        }
    }
    return false;
}

void Game2048::addRandomTile(Board& board) {
    std::vector<std::size_t> empty;
    for (std::size_t i = 0; i < board.cells.size(); ++i) {
        if (board.cells[i] == 0) {
            empty.push_back(i);
        }
    }
    if (empty.empty()) {
        return;
    }

    std::uniform_int_distribution<std::size_t> slotDist(0, empty.size() - 1);
    std::uniform_int_distribution<int> valueDist(1, 10);
    board.cells[empty[slotDist(rng_)]] = valueDist(rng_) == 10 ? 4 : 2;
}
