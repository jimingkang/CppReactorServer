#pragma once

#include "wow_types.h"

namespace wowclient {

class TargetFrame {
public:
    void draw(const TargetSnapshot& target, const std::vector<UnitSnapshot>& units) const;
};

} // namespace wowclient
