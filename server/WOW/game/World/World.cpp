#include "World.h"

namespace wow {

World::World() = default;

void World::initialize() {
    objectMgr_.initialize();
    mapMgr_.initialize();
    initialized_ = true;
}

bool World::initialized() const noexcept {
    return initialized_;
}

const std::string& World::name() const noexcept {
    return name_;
}

const std::string& World::motd() const noexcept {
    return motd_;
}

ObjectMgr& World::objectMgr() noexcept {
    return objectMgr_;
}

const ObjectMgr& World::objectMgr() const noexcept {
    return objectMgr_;
}

MapMgr& World::mapMgr() noexcept {
    return mapMgr_;
}

const MapMgr& World::mapMgr() const noexcept {
    return mapMgr_;
}

std::string World::snapshot(std::size_t playerCount) const {
    return "WORLD name=" + name_ + " players=" + std::to_string(playerCount) + "\n" +
           "MOTD " + motd_ + "\n" +
           objectMgr_.summary() +
           mapMgr_.summary();
}

} // namespace wow
