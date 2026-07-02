#include "realm_screen.h"

#include "ui_theme.h"
#include "world_session.h"

#include "imgui.h"

namespace wowclient {

void RealmScreen::render(WorldSession& session, std::array<char, 64>& hostBuf, int& port, std::array<char, 64>& accountBuf, std::array<char, 64>& passwordBuf) const {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* draw = ImGui::GetBackgroundDrawList();
    draw->AddRectFilledMultiColor({0.0f, 0.0f}, display,
                                  IM_COL32(10, 14, 22, 255),
                                  IM_COL32(14, 18, 30, 255),
                                  IM_COL32(22, 16, 12, 255),
                                  IM_COL32(16, 12, 10, 255));

    ui::beginOverlayPanel("Realm", {display.x * 0.5f - 290.0f, display.y * 0.5f - 210.0f}, {580.0f, 420.0f});
    ImGui::TextColored(ui::gold(), "WOW Client");
    ImGui::TextDisabled("Realm Login");
    ImGui::Separator();

    ImGui::Columns(2, nullptr, false);
    ImGui::SetColumnWidth(0, 280.0f);
    ImGui::InputText("Host", hostBuf.data(), hostBuf.size());
    ImGui::InputInt("Port", &port);
    ImGui::InputText("Account", accountBuf.data(), accountBuf.size());
    ImGui::InputText("Password", passwordBuf.data(), passwordBuf.size(), ImGuiInputTextFlags_Password);
    if (ImGui::Button("Connect", ImVec2(-1.0f, 42.0f))) {
        if (session.connectNow(hostBuf.data(), port)) {
            session.sendAuth(accountBuf.data(), passwordBuf.data());
        }
    }
    if (ImGui::Button("Refresh", ImVec2(-1.0f, 34.0f)) && session.connected()) {
        session.requestRealmList();
    }

    ImGui::NextColumn();
    ImGui::TextColored(ui::softGold(), "Status");
    ImGui::Separator();
    ImGui::TextWrapped("%s", ui::presentText(session.status()).c_str());
    ImGui::Spacing();
    ImGui::TextWrapped("%s", ui::presentText(session.motd()).c_str());
    if (!session.authFailure().empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.90f, 0.38f, 0.30f, 1.0f), "%s", session.authFailure().c_str());
    }
    ImGui::Columns(1);
    ui::endOverlayPanel();
}

} // namespace wowclient
