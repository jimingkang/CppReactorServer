#include "reactor.h"
#include "server.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string host = argc > 1 ? argv[1] : "0.0.0.0";
    const int port = argc > 2 ? std::atoi(argv[2]) : 7777;

    try {
        Reactor reactor;
        GameServer server(reactor, host, port);
        server.start();
        reactor.run();
    } catch (const std::exception& ex) {
        std::cerr << "server error: " << ex.what() << '\n';
        return 1;
    }
    return 0;
}
