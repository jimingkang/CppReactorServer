#include "guess_number_server_binding.h"

#include "guess_number_game_world.h"
#include "guess_number_session_agent.h"

namespace guessnumber {

std::unique_ptr<IGameWorld> GuessNumberServerBinding::createWorld() {
    return std::make_unique<GuessNumberGameWorld>();
}

std::unique_ptr<ISessionAgent> GuessNumberServerBinding::createSessionAgent(int fd) {
    return std::make_unique<GuessNumberSessionAgent>(fd);
}

std::vector<UserCredential> GuessNumberServerBinding::seedUsers() {
    return {};
}

} // namespace guessnumber
