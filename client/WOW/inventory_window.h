#pragma once

#include "wow_types.h"

namespace wowclient {

class WorldSession;

class InventoryWindow {
public:
    void draw(const InventorySnapshot& inventory, WorldSession& session) const;
};

} // namespace wowclient
