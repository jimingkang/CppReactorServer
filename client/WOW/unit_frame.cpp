#include "unit_frame.h"

#include "imgui.h"

namespace wowclient {

void UnitFrame::drawSelf(const UnitSnapshot& unit) const {
    ImGui::Text("%s", unit.name.c_str());
    ImGui::Text("Level %d  %s %s", unit.level, unit.race.c_str(), unit.klass.c_str());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(222, 163, 18, 255));
    ImGui::ProgressBar(unit.hpMax > 0 ? static_cast<float>(unit.hp) / static_cast<float>(unit.hpMax) : 0.0f,
                       ImVec2(-1.0f, 0.0f),
                       ("HP " + std::to_string(unit.hp) + "/" + std::to_string(unit.hpMax)).c_str());
    ImGui::PopStyleColor();
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, IM_COL32(214, 143, 18, 255));
    ImGui::ProgressBar(unit.powerMax > 0 ? static_cast<float>(unit.power) / static_cast<float>(unit.powerMax) : 0.0f,
                       ImVec2(-1.0f, 0.0f),
                       ("Power " + std::to_string(unit.power) + "/" + std::to_string(unit.powerMax)).c_str());
    ImGui::PopStyleColor();
}

void UnitFrame::drawNearby(const std::vector<UnitSnapshot>& units) const {
    ImGui::Text("Nearby Units");
    if (ImGui::BeginTable("nearby_units", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Name");
        ImGui::TableSetupColumn("Level");
        ImGui::TableSetupColumn("HP");
        ImGui::TableHeadersRow();
        for (const UnitSnapshot& unit : units) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", unit.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("Lv.%d", unit.level);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%d/%d", unit.hp, unit.hpMax);
        }
        if (units.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("No nearby units");
        }
        ImGui::EndTable();
    }
}

} // namespace wowclient
