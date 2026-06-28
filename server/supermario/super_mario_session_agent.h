#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace supermario {

class SuperMarioSessionAgent {
public:
    explicit SuperMarioSessionAgent(int fd);

    int fd() const noexcept;
    int playerId() const noexcept;
    bool closing() const noexcept;

    void setPlayerId(int playerId) noexcept;
    void markClosing() noexcept;

    std::vector<std::string> pushSocketData(std::string_view chunk);

private:
    static std::string trimLine(std::string line);

    int fd_ = -1;
    int playerId_ = 0;
    bool closing_ = false;
    std::string input_;
};

} // namespace supermario
