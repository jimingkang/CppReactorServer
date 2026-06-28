#include "super_mario_server_binding.h"

#include "super_mario_game_world.h"
#include "super_mario_session_agent.h"

namespace supermario {

std::unique_ptr<IGameWorld> SuperMarioServerBinding::createWorld() {
    return std::make_unique<SuperMarioGameWorld>();
}

std::unique_ptr<ISessionAgent> SuperMarioServerBinding::createSessionAgent(int fd) {
    return std::make_unique<SuperMarioSessionAgent>(fd);
}

std::vector<UserCredential> SuperMarioServerBinding::seedUsers() {
    return {
        {"mario", "mushroom"},
        {"luigi", "green"},
        {"peach", "castle"},
    };
}

} // namespace supermario
