#include "inventory_window.h"

#include "world_session.h"

#include "imgui.h"

#include <array>

namespace wowclient {

namespace {

constexpr std::array<const char*, 6> kEquipmentSlots = {
    "MainHand", "Chest", "Feet", "Head", "Legs", "OffHand"
};

const ItemSnapshot* equippedItem(const InventorySnapshot& inventory, const char* slot) {
    for (const ItemSnapshot& item : inventory.detailedItems) {
        if (item.equipped && item.slot == slot) {
            return &item;
        }
    }
    return nullptr;
}

} // namespace

void InventoryWindow::draw(const InventorySnapshot& inventory, WorldSession& session) const {
    ImGui::Text("Inventory");
    ImGui::Text("Gold: %d", inventory.gold);
    ImGui::SameLine();
    ImGui::Text("Bag: %zu", inventory.detailedItems.size());

    ImGui::BeginChild("equipment_slots", {0, 138}, true);
    for (const char* slot : kEquipmentSlots) {
        const ItemSnapshot* item = equippedItem(inventory, slot);
        ImGui::Text("%s", slot);
        ImGui::SameLine(110.0f);
        if (item != nullptr) {
            if (ImGui::SmallButton((std::string("Unequip##") + slot).c_str())) {
                session.unequip(slot);
            }
            ImGui::SameLine();
            ImGui::Text("%s", item->name.c_str());
        } else {
            ImGui::TextDisabled("Empty");
        }
    }
    ImGui::EndChild();

    ImGui::BeginChild("bag_items", {0, 174}, true);
    for (const ItemSnapshot& item : inventory.detailedItems) {
        if (item.equipped) {
            continue;
        }
        ImGui::Text("%s", item.name.c_str());
        ImGui::SameLine(150.0f);
        ImGui::TextDisabled("%s", item.type.c_str());
        if (item.slot != "None") {
            ImGui::SameLine(238.0f);
            if (ImGui::SmallButton((std::string("Equip##") + std::to_string(item.id)).c_str())) {
                session.equip(item.id);
            }
        }
        if (item.minDamage > 0 || item.maxDamage > 0) {
            ImGui::TextDisabled("Damage %d-%d  Speed %.1f", item.minDamage, item.maxDamage, item.speed);
        }
    }
    ImGui::EndChild();
}

} // namespace wowclient
