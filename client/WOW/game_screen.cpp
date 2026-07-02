#include "game_screen.h"

#include "action_bar.h"
#include "chat_frame.h"
#include "inventory_window.h"
#include "target_frame.h"
#include "ui_theme.h"
#include "unit_frame.h"
#include "world_session.h"

#include "imgui.h"

namespace wowclient {

void GameScreen::render(WorldSession& session,
                        const Dependencies& deps,
                        bool& showProtocolLog,
                        std::array<char, 128>& commandBuf,
                        std::array<char, 128>& chatBuf,
                        bool& leaveWorldRequested) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    drawWorldScene(session, display);
    drawTopBar(session);
    drawLeftHud(session, deps);
    drawBottomHud(session, deps, commandBuf, chatBuf, leaveWorldRequested);
    drawRightHud(session, deps, showProtocolLog);
}

void GameScreen::drawTopBar(const WorldSession& session) const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = display.x > 900.0f ? 720.0f : display.x - 40.0f;
    ui::beginOverlayPanel("TopBar", {display.x * 0.5f - width * 0.5f, 10.0f}, {width, 42.0f}, 0.38f,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::TextColored(ui::softGold(), "%s", session.map().name.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.28f);
    ImGui::Text("%s", session.activeCharacter().name.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.54f);
    ImGui::TextDisabled("%s, %s", session.map().zone.c_str(), session.map().name.c_str());
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() * 0.84f);
    if (session.instanceId() > 0) {
        ImGui::Text("Realm %d", session.instanceId());
    } else {
        ImGui::Text("World");
    }
    ui::endOverlayPanel();
}

void GameScreen::drawLeftHud(WorldSession& session, const Dependencies& deps) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ui::beginOverlayPanel("PlayerHud", {18.0f, 76.0f}, {286.0f, 156.0f}, 0.44f);
    deps.unitFrame->drawSelf(session.selfUnit());
    drawTargetActions(session);
    ui::endOverlayPanel();

    ui::beginOverlayPanel("TargetHud", {18.0f, 244.0f}, {286.0f, 124.0f}, 0.42f);
    deps.targetFrame->draw(session.target(), session.nearbyUnits());
    ui::endOverlayPanel();

    ui::beginOverlayPanel("UnitRoster", {18.0f, 382.0f}, {286.0f, 170.0f}, 0.38f);
    deps.unitFrame->drawNearby(session.nearbyUnits());
    ui::endOverlayPanel();

    const float inventoryHeight = display.y - 570.0f;
    ui::beginOverlayPanel("InventoryHud", {18.0f, 566.0f}, {286.0f, inventoryHeight > 160.0f ? inventoryHeight : 160.0f}, 0.36f);
    deps.inventoryWindow->draw(session.inventory(), session);
    ui::endOverlayPanel();
}

void GameScreen::drawBottomHud(WorldSession& session, const Dependencies& deps, std::array<char, 128>& commandBuf, std::array<char, 128>& chatBuf, bool& leaveWorldRequested) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ui::beginOverlayPanel("BottomCenterHud", {display.x * 0.5f - 310.0f, display.y - 154.0f}, {620.0f, 132.0f}, 0.52f);
    deps.actionBar->draw(session);
    ui::endOverlayPanel();

    ui::beginOverlayPanel("CombatStrip", {display.x * 0.5f - 290.0f, display.y - 204.0f}, {580.0f, 40.0f}, 0.44f,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (ImGui::Button("Attack", {104.0f, 24.0f})) session.attack();
    ImGui::SameLine();
    if (ImGui::Button("Target", {104.0f, 24.0f})) session.tabTarget();
    ImGui::SameLine();
    if (ImGui::Button("Fireball", {104.0f, 24.0f})) session.cast("Fireball");
    ImGui::SameLine();
    if (ImGui::Button("Spirit", {104.0f, 24.0f})) session.resurrect();
    ImGui::SameLine();
    if (ImGui::Button("Leave", {104.0f, 24.0f})) leaveWorldRequested = true;
    ui::endOverlayPanel();

    ui::beginOverlayPanel("ChatDock", {display.x - 382.0f, display.y - 246.0f}, {364.0f, 228.0f}, 0.34f);
    drawChatInput(session, chatBuf);
    ImGui::Spacing();
    deps.chatFrame->draw(session.chatLog());
    ui::endOverlayPanel();

    (void)commandBuf;
}

void GameScreen::drawRightHud(WorldSession& session, const Dependencies& deps, bool& showProtocolLog) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ui::beginOverlayPanel("MinimapHud", {display.x - 262.0f, 18.0f}, {244.0f, 244.0f}, 0.24f,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    const ImVec2 miniPos = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    const float h = ImGui::GetContentRegionAvail().y;
    const ImVec2 center = {miniPos.x + w * 0.5f, miniPos.y + h * 0.48f};
    const float radius = (w < h ? w : h) * 0.42f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddCircleFilled(center, radius + 16.0f, IM_COL32(18, 14, 10, 220));
    draw->AddCircle(center, radius + 11.0f, IM_COL32(171, 139, 64, 235), 0, 3.0f);
    draw->AddCircle(center, radius + 4.0f, IM_COL32(118, 92, 38, 240), 0, 2.0f);
    draw->AddCircleFilled(center, radius, IM_COL32(16, 68, 28, 210));
    draw->AddCircle(center, radius * 0.56f, IM_COL32(144, 188, 111, 225), 0, 1.8f);
    draw->AddLine({center.x, center.y - radius}, {center.x, center.y + radius}, IM_COL32(255, 255, 255, 26), 1.0f);
    draw->AddLine({center.x - radius, center.y}, {center.x + radius, center.y}, IM_COL32(255, 255, 255, 26), 1.0f);
    draw->AddCircleFilled({center.x - radius * 0.62f + (session.position().x - 48.0f) * 2.0f,
                           center.y - radius * 0.12f + (session.position().y - 48.0f) * 1.5f},
                          4.8f, IM_COL32(235, 191, 72, 255));
    for (const UnitSnapshot& unit : session.nearbyUnits()) {
        draw->AddCircleFilled({center.x - radius * 0.62f + (unit.x - 48.0f) * 2.0f,
                               center.y - radius * 0.12f + (unit.y - 48.0f) * 1.5f},
                              unit.selected ? 4.5f : 3.5f,
                              unit.hostile ? IM_COL32(245, 74, 64, 255) : IM_COL32(110, 182, 255, 255));
    }
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 194.0f);
    ImGui::TextColored(ui::softGold(), "%s", session.map().zone.c_str());
    ImGui::TextDisabled("%s", session.combat().dead ? "Dead" : (session.combat().autoAttacking ? "In Combat" : "Exploring"));
    ui::endOverlayPanel();

    ui::beginOverlayPanel("QuestTracker", {display.x - 320.0f, 280.0f}, {302.0f, 188.0f}, 0.30f);
    drawSystemsTabs(session);
    ui::endOverlayPanel();

    ui::beginOverlayPanel("DebugDock", {display.x - 320.0f, 486.0f}, {302.0f, 70.0f}, 0.22f);
    ImGui::Checkbox("Debug Log", &showProtocolLog);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", session.combat().dead ? "Dead" : (session.combat().autoAttacking ? "Combat" : "Idle"));
    ui::endOverlayPanel();

    if (showProtocolLog) {
        ui::beginOverlayPanel("ProtocolDock", {display.x - 420.0f, display.y - 460.0f}, {402.0f, 196.0f}, 0.26f);
        deps.chatFrame->drawRaw(session.rawLog());
        ui::endOverlayPanel();
    }
}

void GameScreen::drawWorldScene(const WorldSession& session, const ImVec2& size) const {
    const ImVec2 p0 = {0.0f, 0.0f};
    const ImVec2 p1 = {size.x, size.y};
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilledMultiColor(p0, p1,
                                  IM_COL32(22, 30, 58, 255),
                                  IM_COL32(38, 46, 74, 255),
                                  IM_COL32(92, 56, 28, 255),
                                  IM_COL32(70, 42, 18, 255));
    const float horizon = p0.y + size.y * 0.64f;
    draw->AddTriangleFilled({70.0f, horizon + 18.0f}, {340.0f, horizon - 220.0f}, {560.0f, horizon + 18.0f}, IM_COL32(48, 60, 78, 255));
    draw->AddTriangleFilled({420.0f, horizon + 8.0f}, {760.0f, horizon - 246.0f}, {1080.0f, horizon + 8.0f}, IM_COL32(58, 72, 88, 255));
    draw->AddTriangleFilled({900.0f, horizon + 14.0f}, {1180.0f, horizon - 200.0f}, {1420.0f, horizon + 14.0f}, IM_COL32(44, 54, 74, 255));
    draw->AddCircleFilled({p1.x - 170.0f, p0.y + 130.0f}, 56.0f, IM_COL32(219, 194, 118, 245));
    draw->AddCircleFilled({p0.x + 240.0f, p0.y + 142.0f}, 26.0f, IM_COL32(255, 255, 255, 24));
    draw->AddCircleFilled({p0.x + 290.0f, p0.y + 132.0f}, 22.0f, IM_COL32(255, 255, 255, 20));
    draw->AddCircleFilled({p0.x + 328.0f, p0.y + 144.0f}, 18.0f, IM_COL32(255, 255, 255, 18));
    draw->AddRectFilled({p0.x, horizon}, p1, IM_COL32(38, 80, 34, 255));
    draw->AddTriangleFilled({size.x * 0.22f, horizon + 2.0f}, {size.x * 0.52f, size.y - 34.0f}, {size.x * 0.78f, horizon + 2.0f}, IM_COL32(142, 118, 68, 255));
    draw->AddTriangleFilled({size.x * 0.31f, horizon + 2.0f}, {size.x * 0.52f, size.y - 34.0f}, {size.x * 0.69f, horizon + 2.0f}, IM_COL32(186, 160, 104, 255));
    draw->AddText({34.0f, 92.0f}, IM_COL32(236, 224, 196, 215), session.map().zone.c_str());
    draw->AddText({34.0f, 118.0f}, IM_COL32(191, 176, 144, 195), ui::presentText(session.motd()).c_str());

    const float selfX = size.x * 0.5f;
    const float selfY = size.y - 112.0f;
    draw->AddCircleFilled({selfX, selfY}, 14.0f, IM_COL32(235, 191, 72, 255));
    draw->AddCircle({selfX, selfY}, 18.0f, IM_COL32(255, 226, 136, 230), 0, 2.0f);
    draw->AddText({selfX - 28.0f, selfY - 30.0f}, IM_COL32(250, 244, 228, 220), session.activeCharacter().name.c_str());

    for (const UnitSnapshot& unit : session.nearbyUnits()) {
        const float offsetX = (unit.x - session.position().x) * 7.0f;
        const float offsetY = (unit.y - session.position().y) * 2.8f;
        const ImVec2 pos{selfX + offsetX, selfY - offsetY};
        const ImU32 color = unit.hostile ? IM_COL32(222, 78, 72, 255) : IM_COL32(115, 181, 255, 255);
        draw->AddCircleFilled(pos, unit.selected ? 10.0f : 8.0f, color);
        if (unit.selected) {
            draw->AddCircle(pos, 14.0f, IM_COL32(255, 220, 120, 240), 0, 1.5f);
        }
        draw->AddText({pos.x - 22.0f, pos.y - 22.0f}, IM_COL32(230, 230, 230, 220), unit.name.c_str());
    }
}

void GameScreen::drawCommandBar(WorldSession& session, std::array<char, 128>& commandBuf) const {
    (void)session;
    (void)commandBuf;
}

void GameScreen::drawChatInput(WorldSession& session, std::array<char, 128>& chatBuf) const {
    ImGui::TextColored(ui::softGold(), "Chat");
    ImGui::SetNextItemWidth(-90.0f);
    const bool submit = ImGui::InputText("##chat_dock_input", chatBuf.data(), chatBuf.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::Button("Say##chat_dock", {76.0f, 0.0f}) || submit) && chatBuf[0] != '\0') {
        session.say(chatBuf.data());
    }
}

void GameScreen::drawTargetActions(WorldSession& session) {
    const int targetId = session.target().targetId;
    if (targetId == 0) {
        if (ImGui::Button("Tab Target##pick", {120.0f, 26.0f})) {
            session.tabTarget();
        }
        return;
    }

    if (ImGui::Button("Attack##target", {86.0f, 26.0f})) {
        session.attack();
    }
    ImGui::SameLine();
    if (ImGui::Button("Loot##target", {86.0f, 26.0f})) {
        session.loot(targetId);
    }
    ImGui::SameLine();
    if (ImGui::Button("Retarget##target", {86.0f, 26.0f})) {
        session.target(targetId);
    }
}

void GameScreen::drawSystemsTabs(WorldSession& session) {
    if (ImGui::BeginTabBar("world_tabs")) {
        if (ImGui::BeginTabItem("Spellbook")) {
            if (ImGui::Button("Refresh##spellbook", {92.0f, 0.0f})) {
                session.requestSpellbook();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            ImGui::InputText("##train_spell", trainSpellBuf_.data(), trainSpellBuf_.size());
            ImGui::SameLine();
            if (ImGui::Button("Train##spell", {60.0f, 0.0f}) && trainSpellBuf_[0] != '\0') {
                session.train(trainSpellBuf_.data());
            }
            ImGui::Separator();
            for (const SpellSummary& spell : session.spells()) {
                ImGui::Text("%s", ui::presentText(spell.name).c_str());
                ImGui::SameLine(132.0f);
                ImGui::TextDisabled("%d-%d", spell.minDamage, spell.maxDamage);
                ImGui::SameLine(196.0f);
                ImGui::TextDisabled("Cost %d", spell.cost);
                ImGui::SameLine(250.0f);
                const std::string castLabel = "Cast##spell_" + std::to_string(spell.id);
                if (ImGui::SmallButton(castLabel.c_str())) {
                    session.cast(spell.name);
                }
                if (spell.remainingMs > 0) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%dms", spell.remainingMs);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Quests")) {
            if (ImGui::Button("Refresh##quests", {92.0f, 0.0f})) {
                session.requestQuests();
            }
            ImGui::Separator();
            for (const QuestSummary& quest : session.quests()) {
                ImGui::Text("%s", ui::presentText(quest.title).c_str());
                ImGui::TextDisabled("%s  %d/%d", ui::presentText(quest.status).c_str(), quest.progress, quest.required);
                if (quest.status == "available") {
                    const std::string acceptLabel = "Accept##quest_" + std::to_string(quest.id);
                    if (ImGui::SmallButton(acceptLabel.c_str())) {
                        session.acceptQuest(quest.id);
                    }
                } else if (quest.status == "complete") {
                    const std::string turnInLabel = "Turn In##quest_" + std::to_string(quest.id);
                    if (ImGui::SmallButton(turnInLabel.c_str())) {
                        session.turnInQuest(quest.id);
                    }
                }
                ImGui::Spacing();
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Vendor")) {
            if (ImGui::Button("Browse##vendor", {92.0f, 0.0f})) {
                session.requestVendor();
            }
            ImGui::Separator();
            for (const VendorItem& item : session.vendorItems()) {
                ImGui::Text("%s", ui::presentText(item.name).c_str());
                ImGui::SameLine(160.0f);
                ImGui::TextDisabled("%s  %d", item.type.c_str(), item.price);
                ImGui::SameLine(246.0f);
                const std::string buyLabel = "Buy##vendor_" + std::to_string(item.id);
                if (ImGui::SmallButton(buyLabel.c_str())) {
                    session.buyItem(item.id);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gossip")) {
            if (ImGui::Button("Talk##gossip", {92.0f, 0.0f})) {
                session.requestGossip();
            }
            ImGui::Separator();
            for (const GossipOption& option : session.gossipOptions()) {
                if (ImGui::Selectable(ui::presentText(option.text).c_str(), false)) {
                    session.gossipSelect(option.index);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Social")) {
            if (ImGui::Button("Who##social", {92.0f, 0.0f})) {
                session.requestWho();
            }
            ImGui::Separator();
            for (const WhoPlayerSummary& player : session.whoPlayers()) {
                ImGui::Text("%s", player.name.c_str());
                ImGui::SameLine(124.0f);
                ImGui::TextDisabled("Lv.%d %s", player.level, player.klass.c_str());
                ImGui::SameLine(234.0f);
                const std::string label = "Target##who_" + std::to_string(player.id);
                if (ImGui::SmallButton(label.c_str())) {
                    session.target(player.id);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Travel")) {
            ImGui::Text("Map");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::InputInt("##teleport_map", &teleportMapId_);
            ImGui::SameLine();
            if (ImGui::Button("Teleport##travel", {84.0f, 0.0f})) {
                session.teleport(teleportMapId_);
            }
            ImGui::Separator();
            if (ImGui::Button("Northshire##travel", {110.0f, 0.0f})) {
                teleportMapId_ = 0;
                session.teleport(0);
            }
            ImGui::SameLine();
            if (ImGui::Button("Elwynn##travel", {110.0f, 0.0f})) {
                teleportMapId_ = 1;
                session.teleport(1);
            }
            ImGui::Spacing();
            if (ImGui::Button("Release Spirit##travel", {140.0f, 0.0f})) {
                session.release();
            }
            ImGui::SameLine();
            if (ImGui::Button("Resurrect##travel", {110.0f, 0.0f})) {
                session.resurrect();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

} // namespace wowclient
