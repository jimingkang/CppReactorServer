#include "ui_theme.h"

#include <algorithm>

namespace wowclient::ui {

std::string presentText(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    return value;
}

ImVec4 gold() { return {0.88f, 0.72f, 0.30f, 1.0f}; }
ImVec4 softGold() { return {0.70f, 0.57f, 0.24f, 1.0f}; }
ImVec4 muted() { return {0.64f, 0.66f, 0.70f, 1.0f}; }
ImVec4 parchment() { return {0.86f, 0.78f, 0.60f, 1.0f}; }
ImVec4 accentBlue() { return {0.27f, 0.43f, 0.73f, 1.0f}; }
ImVec4 panelBg(float alpha) { return {0.08f, 0.06f, 0.04f, alpha}; }
ImVec4 panelBorder(float alpha) { return {0.56f, 0.42f, 0.18f, alpha}; }

void beginOverlayPanel(const char* name, const ImVec2& pos, const ImVec2& size, float alpha, ImGuiWindowFlags extra) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 9.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, panelBg(alpha));
    ImGui::PushStyleColor(ImGuiCol_Border, panelBorder(alpha));
    ImGui::Begin(name, nullptr,
                 ImGuiWindowFlags_NoTitleBar |
                 ImGuiWindowFlags_NoCollapse |
                 ImGuiWindowFlags_NoResize |
                 ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings |
                 extra);
}

void endOverlayPanel() {
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

} // namespace wowclient::ui
