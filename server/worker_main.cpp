#include "worker_server.h"
#include "supermario/super_mario_server_binding.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "0.0.0.0";
    const int port = argc > 2 ? std::atoi(argv[2]) : 7779;
    const std::size_t workers = argc > 3 ? static_cast<std::size_t>(std::atoi(argv[3]))
                                         : std::max(1u, std::thread::hardware_concurrency());

    try {
        WorkerGameServer server(host, port, workers, std::make_unique<supermario::SuperMarioServerBinding>());
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "worker_game_server error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
