#pragma once

#include "../Globals/ObjectMgr.h"
#include "../Maps/MapMgr.h"

#include <cstddef>
#include <string>

namespace wow {

class World {
public:
    World();

    void initialize();

    bool initialized() const noexcept;
    const std::string& name() const noexcept;
    const std::string& motd() const noexcept;

    ObjectMgr& objectMgr() noexcept;
    const ObjectMgr& objectMgr() const noexcept;
    MapMgr& mapMgr() noexcept;
    const MapMgr& mapMgr() const noexcept;

    std::string snapshot(std::size_t playerCount) const;

private:
    std::string name_ = "wow-shell";
    std::string motd_ = "Welcome to the WOW shell worldserver.";
    bool initialized_ = false;
    ObjectMgr objectMgr_;
    MapMgr mapMgr_;
};

} // namespace wow
