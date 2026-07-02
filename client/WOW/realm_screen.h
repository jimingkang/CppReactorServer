#pragma once

#include <array>

namespace wowclient {

class WorldSession;

class RealmScreen {
public:
    void render(WorldSession& session, std::array<char, 64>& hostBuf, int& port, std::array<char, 64>& accountBuf, std::array<char, 64>& passwordBuf) const;
};

} // namespace wowclient
