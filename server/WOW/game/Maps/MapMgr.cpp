#include "MapMgr.h"

#include <algorithm>
#include <sstream>

namespace wow {

void MapMgr::initialize() {
    maps_.clear();
    maps_.emplace(0, MapDefinition{0, "Azeroth", "Northshire", 48.0f, 48.0f});
    maps_.emplace(1, MapDefinition{1, "Azeroth", "Elwynn", 64.0f, 44.0f});
    starterMapId_ = 0;
    starterMapName_ = "Azeroth";
    starterZoneName_ = "Northshire";
    initialized_ = true;
}

bool MapMgr::initialized() const noexcept {
    return initialized_;
}

int MapMgr::starterMapId() const noexcept {
    return starterMapId_;
}

const std::string& MapMgr::starterMapName() const noexcept {
    return starterMapName_;
}

const std::string& MapMgr::starterZoneName() const noexcept {
    return starterZoneName_;
}

const MapMgr::MapDefinition* MapMgr::findMap(int mapId) const noexcept {
    const auto it = maps_.find(mapId);
    return it == maps_.end() ? nullptr : &it->second;
}

MapMgr::MapInstance& MapMgr::ensureInstance(int mapId, int instanceId) {
    const MapDefinition* map = findMap(mapId);
    InstanceKey key = makeInstanceKey(mapId, instanceId);
    MapInstance& instance = instances_[key];
    if (instance.name.empty()) {
        instance.mapId = mapId;
        instance.instanceId = instanceId;
        instance.name = map ? map->name : "Unknown";
        instance.zone = map ? map->zone : "Unknown";
    }
    return instance;
}

void MapMgr::addPlayer(int mapId, int instanceId, int playerId) {
    MapInstance& instance = ensureInstance(mapId, instanceId);
    if (std::find(instance.playerIds.begin(), instance.playerIds.end(), playerId) == instance.playerIds.end()) {
        instance.playerIds.push_back(playerId);
    }
}

void MapMgr::removePlayer(int mapId, int instanceId, int playerId) {
    const InstanceKey key = makeInstanceKey(mapId, instanceId);
    const auto it = instances_.find(key);
    if (it == instances_.end()) {
        return;
    }
    auto& players = it->second.playerIds;
    players.erase(std::remove(players.begin(), players.end(), playerId), players.end());
    if (players.empty()) {
        instances_.erase(it);
    }
}

std::vector<int> MapMgr::playersInInstance(int mapId, int instanceId) const {
    const InstanceKey key = makeInstanceKey(mapId, instanceId);
    const auto it = instances_.find(key);
    return it == instances_.end() ? std::vector<int>{} : it->second.playerIds;
}

std::string MapMgr::instanceSummary(int mapId, int instanceId) const {
    const InstanceKey key = makeInstanceKey(mapId, instanceId);
    const auto it = instances_.find(key);
    if (it == instances_.end()) {
        return "MAP_INSTANCE map=" + std::to_string(mapId) + " instance=" + std::to_string(instanceId) + " players=0\n";
    }

    std::ostringstream out;
    out << "MAP_INSTANCE map=" << it->second.mapId
        << " instance=" << it->second.instanceId
        << " name=" << it->second.name
        << " zone=" << it->second.zone
        << " players=" << it->second.playerIds.size() << "\n";
    return out.str();
}

std::string MapMgr::summary() const {
    std::ostringstream out;
    out << "MAP_MGR initialized=" << (initialized_ ? 1 : 0)
        << " starter_map=" << starterMapId_
        << " map_name=" << starterMapName_
        << " zone=" << starterZoneName_
        << " loaded_maps=" << maps_.size()
        << " active_instances=" << instances_.size() << "\n";
    return out.str();
}

MapMgr::InstanceKey MapMgr::makeInstanceKey(int mapId, int instanceId) {
    return std::to_string(mapId) + ":" + std::to_string(instanceId);
}

} // namespace wow
