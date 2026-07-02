#include "character_screen.h"

#include "ui_theme.h"
#include "world_session.h"

#include "imgui.h"

namespace wowclient {

void CharacterScreen::render(WorldSession& session,
                             int& selectedCharacter,
                             std::array<char, 64>& createNameBuf,
                             std::array<char, 32>& createRaceBuf,
                             std::array<char, 32>& createClassBuf,
                             std::array<char, 32>& createGenderBuf,
                             bool& disconnectRequested) const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilledMultiColor({0.0f, 0.0f}, display,
                                  IM_COL32(12, 16, 24, 255),
                                  IM_COL32(14, 20, 32, 255),
                                  IM_COL32(28, 20, 12, 255),
                                  IM_COL32(18, 14, 10, 255));

    ui::beginOverlayPanel("CharacterSelect", {24.0f, 24.0f}, {display.x - 48.0f, display.y - 48.0f});
    ImGui::TextColored(ui::gold(), "%s", session.selectedRealm().name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("Account: %s", session.accountName().c_str());
    ImGui::Separator();

    ImGui::Columns(3, nullptr, false);
    ImGui::SetColumnWidth(0, 340.0f);
    ImGui::SetColumnWidth(1, (display.x - 48.0f) * 0.34f);

    ImGui::BeginChild("char_list", {0, 0}, true);
    ImGui::TextColored(ui::softGold(), "Characters");
    ImGui::Separator();
    for (int i = 0; i < static_cast<int>(session.characters().size()); ++i) {
        const CharacterSummary& character = session.characters()[static_cast<std::size_t>(i)];
        if (ImGui::Selectable(character.name.c_str(), selectedCharacter == i, 0, ImVec2(-1.0f, 58.0f))) {
            selectedCharacter = i;
        }
        ImGui::SameLine(210.0f);
        ImGui::TextDisabled("Lv.%d %s", character.level, character.klass.c_str());
    }
    ImGui::Spacing();
    const bool canEnterWorld = !session.characters().empty() && !session.enteringWorld();
    if (!canEnterWorld) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button(session.enteringWorld() ? "Entering..." : "Enter World", ImVec2(-1.0f, 42.0f)) && !session.characters().empty()) {
        session.enterWorld(session.characters()[static_cast<std::size_t>(selectedCharacter)].name);
    }
    if (!canEnterWorld) {
        ImGui::EndDisabled();
    }
    if (ImGui::Button("Refresh Roster", ImVec2(-1.0f, 34.0f)) && !session.enteringWorld()) {
        session.requestCharacterList();
    }
    if (ImGui::Button("Disconnect", ImVec2(-1.0f, 34.0f))) {
        disconnectRequested = true;
    }
    if (session.enteringWorld()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Waiting for world join...");
        if (session.queuedCommandCount() > 0) {
            ImGui::TextDisabled("Queued commands: %d", session.queuedCommandCount());
        }
    }
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::BeginChild("char_preview", {0, 0}, true);
    const CharacterSummary* active = session.characters().empty()
                                         ? &session.activeCharacter()
                                         : &session.characters()[static_cast<std::size_t>(selectedCharacter)];
    ImGui::TextColored(ui::gold(), "%s", active->name.c_str());
    ImGui::Text("Level %d %s %s", active->level, active->race.c_str(), active->klass.c_str());
    ImGui::TextDisabled("%s", active->gender.c_str());
    ImGui::Separator();
    ImGui::BeginChild("char_stage", {0, 280}, true);
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 p1 = {p0.x + ImGui::GetContentRegionAvail().x, p0.y + ImGui::GetContentRegionAvail().y};
    ImDrawList* preview = ImGui::GetWindowDrawList();
    preview->AddRectFilledMultiColor(p0, p1,
                                     IM_COL32(15, 22, 36, 255),
                                     IM_COL32(18, 28, 42, 255),
                                     IM_COL32(24, 18, 12, 255),
                                     IM_COL32(18, 14, 10, 255));
    preview->AddCircleFilled({(p0.x + p1.x) * 0.5f, p0.y + 120.0f}, 44.0f, IM_COL32(205, 185, 115, 255));
    preview->AddRectFilled({p0.x, p0.y + 210.0f}, p1, IM_COL32(36, 58, 34, 255));
    preview->AddCircleFilled({(p0.x + p1.x) * 0.5f, p0.y + 168.0f}, 16.0f, IM_COL32(224, 191, 102, 255));
    preview->AddRectFilled({(p0.x + p1.x) * 0.5f - 18.0f, p0.y + 184.0f},
                           {(p0.x + p1.x) * 0.5f + 18.0f, p0.y + 248.0f},
                           IM_COL32(84, 98, 130, 255), 5.0f);
    ImGui::EndChild();
    ImGui::Spacing();
    ImGui::Text("Zone: %s", active->zone.c_str());
    if (session.enteringWorld()) {
        ImGui::TextColored(ui::softGold(), "Entering %s", active->name.c_str());
    }
    ImGui::Text("Population: %s", session.selectedRealm().population.c_str());
    ImGui::TextWrapped("%s", ui::presentText(session.motd()).c_str());
    ImGui::EndChild();

    ImGui::NextColumn();
    ImGui::BeginChild("char_create", {0, 0}, true);
    ImGui::TextColored(ui::softGold(), "Create Character");
    ImGui::Separator();
    ImGui::InputText("Name", createNameBuf.data(), createNameBuf.size());
    ImGui::InputText("Race", createRaceBuf.data(), createRaceBuf.size());
    ImGui::InputText("Class", createClassBuf.data(), createClassBuf.size());
    ImGui::InputText("Gender", createGenderBuf.data(), createGenderBuf.size());
    if (ImGui::Button("Create", ImVec2(-1.0f, 40.0f))) {
        session.createCharacter(createNameBuf.data(), createRaceBuf.data(), createClassBuf.data(), createGenderBuf.data());
    }
    ImGui::EndChild();

    ImGui::Columns(1);
    ui::endOverlayPanel();
}

} // namespace wowclient
