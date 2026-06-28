#include "super_mario_session_agent.h"

namespace supermario {

SuperMarioSessionAgent::SuperMarioSessionAgent(int fd) : fd_(fd) {}

int SuperMarioSessionAgent::fd() const noexcept {
    return fd_;
}

int SuperMarioSessionAgent::playerId() const noexcept {
    return playerId_;
}

bool SuperMarioSessionAgent::closing() const noexcept {
    return closing_;
}

void SuperMarioSessionAgent::setPlayerId(int playerId) noexcept {
    playerId_ = playerId;
}

void SuperMarioSessionAgent::markClosing() noexcept {
    closing_ = true;
}

std::vector<std::string> SuperMarioSessionAgent::pushSocketData(std::string_view chunk) {
    input_.append(chunk.data(), chunk.size());

    std::vector<std::string> lines;
    std::size_t pos = 0;
    while ((pos = input_.find('\n')) != std::string::npos) {
        std::string line = trimLine(input_.substr(0, pos + 1));
        input_.erase(0, pos + 1);
        if (!line.empty()) {
            lines.push_back(std::move(line));
        }
    }
    return lines;
}

std::string SuperMarioSessionAgent::trimLine(std::string line) {
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
        line.pop_back();
    }
    return line;
}

} // namespace supermario
