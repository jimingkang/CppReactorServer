#include "action_bar.h"

#include "world_session.h"

#include "imgui.h"

#include <algorithm>
#include <string>

namespace wowclient {

namespace {

std::string commandFromLabel(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    return value;
}

std::string prettyLabel(std::string value) {
    std::replace(value.begin(), value.end(), '_', ' ');
    if (value == "ATTACK") return "Attack";
    if (value == "TAB TARGET") return "Tab";
    if (value == "QUEST LIST") return "Quest";
    if (value == "VENDOR LIST") return "Vendor";
    if (value == "MOVE W") return "Forward";
    if (value == "MOVE S") return "Back";
    if (value == "MOVE A") return "Left";
    if (value == "MOVE D") return "Right";
    if (value.rfind("TARGET ", 0) == 0) return "Target";
    return value;
}

} // namespace

void ActionBar::draw(WorldSession& session) const {
    const auto& actions = session.actionBar();
    const float buttonWidth = 96.0f;
    for (std::size_t i = 0; i < actions.size(); ++i) {
        const ActionButton& action = actions[i];
        const std::string label = prettyLabel(action.label);
        if (ImGui::Button(label.c_str(), ImVec2(buttonWidth, 34.0f))) {
            session.sendCommand(commandFromLabel(action.command));
        }
        if ((i + 1) % 4 != 0 && i + 1 < actions.size()) {
            ImGui::SameLine();
        }
    }
}

} // namespace wowclient
