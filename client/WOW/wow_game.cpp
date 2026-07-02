#include "wow_game.h"

#include "ui_theme.h"
#include "world_session.h"

namespace wowclient {

WowGame::WowGame() {
    ui::setBuffer(hostBuf_, "127.0.0.1");
    ui::setBuffer(accountBuf_, "player");
    ui::setBuffer(passwordBuf_, "player");
    ui::setBuffer(createNameBuf_, "NewHero");
    ui::setBuffer(createRaceBuf_, "Human");
    ui::setBuffer(createClassBuf_, "Warrior");
    ui::setBuffer(createGenderBuf_, "Nonbinary");
    ui::setBuffer(commandBuf_, "STATE");
    ui::setBuffer(chatBuf_, "Hello Northshire");
}

void WowGame::applyStyle() const {
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 3.0f;
    style.TabRounding = 3.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 1.0f;
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.WindowPadding = ImVec2(10.0f, 10.0f);

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.06f, 0.04f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.08f, 0.05f, 0.92f);
    colors[ImGuiCol_Border] = ui::panelBorder();
    colors[ImGuiCol_FrameBg] = ImVec4(0.14f, 0.10f, 0.07f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.14f, 0.08f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.18f, 0.10f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.42f, 0.26f, 0.10f, 0.98f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.54f, 0.34f, 0.13f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.63f, 0.41f, 0.17f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.18f, 0.23f, 0.36f, 0.92f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.31f, 0.50f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.27f, 0.38f, 0.60f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.19f, 0.29f, 0.98f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.29f, 0.44f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.27f, 0.40f, 0.61f, 1.0f);
    colors[ImGuiCol_Text] = ImVec4(0.92f, 0.86f, 0.72f, 1.0f);
    colors[ImGuiCol_TextDisabled] = ui::muted();
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.08f, 0.05f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.08f, 0.05f, 1.0f);
}

void WowGame::sync(WorldSession& session) {
    if (!session.connected() && stage_ != Stage::Realm) {
        stage_ = Stage::Realm;
    }
    if (session.connected() && session.authenticated() && stage_ == Stage::Realm) {
        stage_ = Stage::Character;
    }
    if (session.worldEntered()) {
        stage_ = Stage::World;
    }
    const int maxIndex = static_cast<int>(session.characters().size()) - 1;
    selectedCharacter_ = std::clamp(selectedCharacter_, 0, std::max(maxIndex, 0));
}

void WowGame::render(WorldSession& session) {
    switch (stage_) {
        case Stage::Realm:
            renderRealmScreen(session);
            break;
        case Stage::Character:
            renderCharacterScreen(session);
            break;
        case Stage::World:
            renderWorldScreen(session);
            break;
    }
}

void WowGame::renderRealmScreen(WorldSession& session) {
    realmScreen_.render(session, hostBuf_, port_, accountBuf_, passwordBuf_);
}

void WowGame::renderCharacterScreen(WorldSession& session) {
    bool disconnectRequested = false;
    characterScreen_.render(session, selectedCharacter_, createNameBuf_, createRaceBuf_, createClassBuf_, createGenderBuf_, disconnectRequested);
    if (disconnectRequested) {
        session.disconnect();
        stage_ = Stage::Realm;
    }
}

void WowGame::renderWorldScreen(WorldSession& session) {
    bool leaveWorldRequested = false;
    GameScreen::Dependencies deps{
        .chatFrame = &chatFrame_,
        .inventoryWindow = &inventoryWindow_,
        .targetFrame = &targetFrame_,
        .unitFrame = &unitFrame_,
        .actionBar = &actionBar_,
    };
    gameScreen_.render(session, deps, showProtocolLog_, commandBuf_, chatBuf_, leaveWorldRequested);
    if (leaveWorldRequested) {
        session.quit();
        stage_ = Stage::Realm;
    }
}

const char* WowGame::stageTitle(Stage stage) {
    switch (stage) {
        case Stage::Realm:
            return "Realm";
        case Stage::Character:
            return "Character";
        case Stage::World:
            return "World";
    }
    return "Unknown";
}

} // namespace wowclient
