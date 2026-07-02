#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace wow {

class ObjectMgr {
public:
    struct RealmRecord {
        std::string name;
        std::string address;
        std::string population;
    };

    struct CharacterTemplate {
        std::string name;
        std::string race;
        std::string klass;
        std::string gender;
        int level = 1;
        int mapId = 0;
        std::string zone;
        float x = 48.0f;
        float y = 48.0f;
        float z = 0.0f;
        float o = 0.0f;
    };

    struct CreatureTemplate {
        int id = 0;
        std::string name;
        std::string race;
        std::string klass;
        std::string faction;
        std::string zone;
        int level = 1;
        int hp = 100;
        int power = 0;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float o = 0.0f;
        bool hostile = false;
        bool vendor = false;
        bool gossip = false;
        bool trainer = false;
    };

    struct VendorItemRecord {
        int id = 0;
        std::string name;
        int price = 0;
        std::string type;
    };

    struct GossipOptionRecord {
        int index = 0;
        std::string text;
    };

    void initialize();

    bool initialized() const noexcept;
    const RealmRecord& realm() const noexcept;
    const std::vector<CharacterTemplate>& characterTemplates() const noexcept;
    const CharacterTemplate* findCharacterTemplate(const std::string& name) const noexcept;
    const std::unordered_map<int, CreatureTemplate>& creatureTemplates() const noexcept;
    const std::vector<VendorItemRecord>& vendorItems() const noexcept;
    const std::vector<GossipOptionRecord>& gossipOptions() const noexcept;
    std::string summary() const;

private:
    bool initialized_ = false;
    RealmRecord realm_{"LocalDev", "127.0.0.1", "low"};
    std::vector<CharacterTemplate> characters_;
    std::unordered_map<int, CreatureTemplate> creatures_;
    std::vector<VendorItemRecord> vendorItems_;
    std::vector<GossipOptionRecord> gossipOptions_;
};

} // namespace wow
