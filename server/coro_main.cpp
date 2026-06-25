#include "coro_reactor.h"
#include "coro_server.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const int port = argc > 2 ? std::atoi(argv[2]) : 7778;

    try {
        CoroReactor reactor;
        CoroGameServer server(reactor, host, port);
        server.start();
        reactor.run();
    } catch (const std::exception& ex) {
        std::cerr << "game_server_coro error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
