#include "WorldSocket.h"

#include <sstream>
#include <string>

namespace wow {

WowOpcode parseWowOpcode(std::string_view line) {
    std::istringstream in{std::string(line)};
    std::string op;
    in >> op;

    if (op == "AUTH") {
        return WowOpcode::Auth;
    }
    if (op == "REALM_LIST") {
        return WowOpcode::RealmList;
    }
    if (op == "CHAR_LIST") {
        return WowOpcode::CharList;
    }
    if (op == "CREATE_CHAR") {
        return WowOpcode::CreateChar;
    }
    if (op == "ENTER_WORLD") {
        return WowOpcode::EnterWorld;
    }
    if (op == "STATE") {
        return WowOpcode::State;
    }
    if (op == "PING") {
        return WowOpcode::Ping;
    }
    if (op == "HELP") {
        return WowOpcode::Help;
    }
    if (op == "QUIT") {
        return WowOpcode::Quit;
    }
    return WowOpcode::Unknown;
}

WowOpcode WorldSocketRouter::route(std::string_view line) const {
    return parseWowOpcode(line);
}

} // namespace wow
