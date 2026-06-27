#pragma once

#include "game_world.h"
#include "reactor.h"
#include "task.h"

#include <memory>
#include <string>

class Server {
public:
    Server(Reactor& reactor, std::string host, int port, std::unique_ptr<GameWorld> world);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void start();

private:
    Task acceptLoop();
    Task clientSession(int clientFd);

    Reactor& reactor_;
    std::unique_ptr<GameWorld> world_;
    std::string host_;
    int port_ = 0;
    int listenFd_ = -1;
};
