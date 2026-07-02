#pragma once

#include "imgui.h"

#include <array>
#include <cstring>
#include <string>

namespace wowclient::ui {

std::string presentText(std::string value);

ImVec4 gold();
ImVec4 softGold();
ImVec4 muted();
ImVec4 parchment();
ImVec4 accentBlue();
ImVec4 panelBg(float alpha = 0.78f);
ImVec4 panelBorder(float alpha = 0.48f);

template <std::size_t N>
void setBuffer(std::array<char, N>& buffer, const char* text) {
    buffer.fill('\0');
    std::strncpy(buffer.data(), text, buffer.size() - 1);
}

void beginOverlayPanel(const char* name, const ImVec2& pos, const ImVec2& size, float alpha = 0.82f, ImGuiWindowFlags extra = 0);
void endOverlayPanel();

} // namespace wowclient::ui
