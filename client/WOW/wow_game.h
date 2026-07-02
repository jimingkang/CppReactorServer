#pragma once

#include "action_bar.h"
#include "character_screen.h"
#include "chat_frame.h"
#include "game_screen.h"
#include "inventory_window.h"
#include "realm_screen.h"
#include "target_frame.h"
#include "unit_frame.h"

#include "imgui.h"

#include <array>

namespace wowclient {

class WorldSession;

class WowGame {
public:
    WowGame();

    void applyStyle() const;
    void sync(WorldSession& session);
    void render(WorldSession& session);

private:
    enum class Stage {
        Realm,
        Character,
        World,
    };

    void renderRealmScreen(WorldSession& session);
    void renderCharacterScreen(WorldSession& session);
    void renderWorldScreen(WorldSession& session);

    static const char* stageTitle(Stage stage);

    ChatFrame chatFrame_;
    InventoryWindow inventoryWindow_;
    TargetFrame targetFrame_;
    UnitFrame unitFrame_;
    ActionBar actionBar_;
    RealmScreen realmScreen_;
    CharacterScreen characterScreen_;
    GameScreen gameScreen_;
    Stage stage_ = Stage::Realm;
    int selectedCharacter_ = 0;
    bool showProtocolLog_ = false;
    std::array<char, 64> hostBuf_{};
    std::array<char, 64> accountBuf_{};
    std::array<char, 64> passwordBuf_{};
    std::array<char, 64> createNameBuf_{};
    std::array<char, 32> createRaceBuf_{};
    std::array<char, 32> createClassBuf_{};
    std::array<char, 32> createGenderBuf_{};
    std::array<char, 128> commandBuf_{};
    std::array<char, 128> chatBuf_{};
    int port_ = 8085;
};

} // namespace wowclient
