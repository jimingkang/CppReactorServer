#pragma once

#include "imgui.h"

#include <array>

namespace wowclient {

class ActionBar;
class ChatFrame;
class InventoryWindow;
class TargetFrame;
class UnitFrame;
class WorldSession;

class GameScreen {
public:
    struct Dependencies {
        ChatFrame* chatFrame = nullptr;
        InventoryWindow* inventoryWindow = nullptr;
        TargetFrame* targetFrame = nullptr;
        UnitFrame* unitFrame = nullptr;
        ActionBar* actionBar = nullptr;
    };

    void render(WorldSession& session,
                const Dependencies& deps,
                bool& showProtocolLog,
                std::array<char, 128>& commandBuf,
                std::array<char, 128>& chatBuf,
                bool& leaveWorldRequested);

private:
    void drawTopBar(const WorldSession& session) const;
    void drawLeftHud(WorldSession& session, const Dependencies& deps);
    void drawBottomHud(WorldSession& session, const Dependencies& deps, std::array<char, 128>& commandBuf, std::array<char, 128>& chatBuf, bool& leaveWorldRequested);
    void drawRightHud(WorldSession& session, const Dependencies& deps, bool& showProtocolLog);
    void drawWorldScene(const WorldSession& session, const ImVec2& size) const;
    void drawCommandBar(WorldSession& session, std::array<char, 128>& commandBuf) const;
    void drawChatInput(WorldSession& session, std::array<char, 128>& chatBuf) const;
    void drawTargetActions(WorldSession& session);
    void drawSystemsTabs(WorldSession& session);

    std::array<char, 32> trainSpellBuf_{{'H', 'o', 'l', 'y', 'L', 'i', 'g', 'h', 't', '\0'}};
    int teleportMapId_ = 0;
};

} // namespace wowclient
