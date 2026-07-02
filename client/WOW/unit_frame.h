#pragma once

#include "wow_types.h"

#include <vector>

namespace wowclient {

class UnitFrame {
public:
    void drawSelf(const UnitSnapshot& unit) const;
    void drawNearby(const std::vector<UnitSnapshot>& units) const;
};

} // namespace wowclient
