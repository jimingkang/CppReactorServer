#pragma once

#include "Opcodes.h"

namespace wow {

class WorldSocketRouter {
public:
    WowOpcode route(std::string_view line) const;
};

} // namespace wow
