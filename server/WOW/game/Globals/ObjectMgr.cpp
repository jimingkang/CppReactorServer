#include "ObjectMgr.h"

#include <sstream>

namespace wow {

void ObjectMgr::initialize() {
    characters_ = {
        {"player_Warrior", "Human", "Warrior", "Nonbinary", 12, 0, "Northshire", 48.0f, 48.0f, 0.0f, 0.0f},
        {"TrainingMage", "Human", "Mage", "Female", 8, 0, "Elwynn", 52.0f, 44.0f, 0.0f, 0.0f},
        {"NorthshireRogue", "Human", "Rogue", "Male", 6, 0, "Northshire", 46.0f, 51.0f, 0.0f, 0.0f},
    };

    creatures_.clear();
    creatures_.emplace(9001, CreatureTemplate{9001, "Northshire_Wolf", "Wolf", "Beast", "Hostile", "Northshire", 4, 72, 0, 58.0f, 45.0f, 0.0f, 0.0f, true, false, false, false});
    creatures_.emplace(9002, CreatureTemplate{9002, "Riverpaw_Scout", "Gnoll", "Rogue", "Hostile", "Northshire", 6, 84, 30, 64.0f, 54.0f, 0.0f, 0.0f, true, false, false, false});
    creatures_.emplace(9100, CreatureTemplate{9100, "Marshal_McBride", "Human", "NPC", "Alliance", "Northshire", 10, 100, 0, 43.0f, 47.0f, 0.0f, 0.0f, false, false, true, true});
    creatures_.emplace(9101, CreatureTemplate{9101, "Brother_Danil", "Human", "Vendor", "Alliance", "Northshire", 8, 100, 0, 46.0f, 42.0f, 0.0f, 0.0f, false, true, true, false});

    vendorItems_ = {
        {3001, "CoarseBread", 3, "Food"},
        {3002, "TrainingDagger", 12, "Weapon"},
        {3003, "MinorHealingPotion", 8, "Consumable"},
    };

    gossipOptions_ = {
        {0, "Where_is_the_training_ground?"},
        {1, "Show_me_your_goods."},
        {2, "I_need_work."},
    };

    initialized_ = true;
}

bool ObjectMgr::initialized() const noexcept {
    return initialized_;
}

const ObjectMgr::RealmRecord& ObjectMgr::realm() const noexcept {
    return realm_;
}

const std::vector<ObjectMgr::CharacterTemplate>& ObjectMgr::characterTemplates() const noexcept {
    return characters_;
}

const ObjectMgr::CharacterTemplate* ObjectMgr::findCharacterTemplate(const std::string& name) const noexcept {
    for (const CharacterTemplate& character : characters_) {
        if (character.name == name) {
            return &character;
        }
    }
    return nullptr;
}

const std::unordered_map<int, ObjectMgr::CreatureTemplate>& ObjectMgr::creatureTemplates() const noexcept {
    return creatures_;
}

const std::vector<ObjectMgr::VendorItemRecord>& ObjectMgr::vendorItems() const noexcept {
    return vendorItems_;
}

const std::vector<ObjectMgr::GossipOptionRecord>& ObjectMgr::gossipOptions() const noexcept {
    return gossipOptions_;
}

std::string ObjectMgr::summary() const {
    std::ostringstream out;
    out << "OBJECT_MGR initialized=" << (initialized_ ? 1 : 0)
        << " characters=" << characters_.size()
        << " creatures=" << creatures_.size()
        << " vendors=" << vendorItems_.size()
        << " gossips=" << gossipOptions_.size() << "\n";
    return out.str();
}

} // namespace wow
