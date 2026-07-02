#include "target_frame.h"

#include "imgui.h"

namespace wowclient {

void TargetFrame::draw(const TargetSnapshot& target, const std::vector<UnitSnapshot>& units) const {
    ImGui::Text("Target");
    ImGui::BeginChild("target_frame", {0, 112}, true);
    const UnitSnapshot* current = nullptr;
    for (const UnitSnapshot& unit : units) {
        if (unit.playerId == target.targetId) {
            current = &unit;
            break;
        }
    }

    if (current == nullptr) {
        ImGui::TextDisabled("No target selected");
        ImGui::EndChild();
        return;
    }

    ImGui::Text("%s", current->name.c_str());
    ImGui::Text("Lv.%d %s %s", current->level, current->race.c_str(), current->klass.c_str());
    ImGui::TextColored(current->hostile ? ImVec4(0.92f, 0.34f, 0.28f, 1.0f) : ImVec4(0.48f, 0.78f, 0.52f, 1.0f),
                       "%s",
                       current->hostile ? "Hostile" : "Friendly");
    ImGui::ProgressBar(current->hpMax > 0 ? static_cast<float>(current->hp) / static_cast<float>(current->hpMax) : 0.0f,
                       ImVec2(-1.0f, 0.0f),
                       ("HP " + std::to_string(current->hp) + "/" + std::to_string(current->hpMax)).c_str());
    ImGui::TextDisabled("Target ID %d", target.targetId);
    ImGui::EndChild();
}

} // namespace wowclient
