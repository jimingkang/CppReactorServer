#pragma once

#include <string_view>

namespace wow {

enum class WowOpcode {
    Auth,
    RealmList,
    CharList,
    CreateChar,
    EnterWorld,
    State,
    Ping,
    Help,
    Quit,
    Unknown,
};

WowOpcode parseWowOpcode(std::string_view line);

} // namespace wow
