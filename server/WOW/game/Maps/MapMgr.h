#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace wow {

class MapMgr {
public:
    struct MapDefinition {
        int mapId = 0;
        std::string name;
        std::string zone;
        float originX = 0.0f;
        float originY = 0.0f;
    };

    struct MapInstance {
        int mapId = 0;
        int instanceId = 1;
        std::string name;
        std::string zone;
        std::vector<int> playerIds;
    };

    void initialize();

    bool initialized() const noexcept;
    int starterMapId() const noexcept;
    const std::string& starterMapName() const noexcept;
    const std::string& starterZoneName() const noexcept;
    const MapDefinition* findMap(int mapId) const noexcept;
    MapInstance& ensureInstance(int mapId, int instanceId = 1);
    void addPlayer(int mapId, int instanceId, int playerId);
    void removePlayer(int mapId, int instanceId, int playerId);
    std::vector<int> playersInInstance(int mapId, int instanceId) const;
    std::string instanceSummary(int mapId, int instanceId) const;
    std::string summary() const;

private:
    using InstanceKey = std::string;

    static InstanceKey makeInstanceKey(int mapId, int instanceId);

    bool initialized_ = false;
    int starterMapId_ = 0;
    std::string starterMapName_ = "Azeroth";
    std::string starterZoneName_ = "Northshire";
    std::unordered_map<int, MapDefinition> maps_;
    std::unordered_map<InstanceKey, MapInstance> instances_;
};

} // namespace wow
