#pragma once

#include <array>

namespace wowclient {

class WorldSession;

class CharacterScreen {
public:
    void render(WorldSession& session,
                int& selectedCharacter,
                std::array<char, 64>& createNameBuf,
                std::array<char, 32>& createRaceBuf,
                std::array<char, 32>& createClassBuf,
                std::array<char, 32>& createGenderBuf,
                bool& disconnectRequested) const;
};

} // namespace wowclient
