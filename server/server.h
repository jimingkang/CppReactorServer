#pragma once

#include "game_world.h"
#include "reactor.h"
#include "task.h"

#include <string>

class GameServer {
public:
    GameServer(Reactor& reactor, std::string host, int port);
    ~GameServer();

    GameServer(const GameServer&) = delete;
    GameServer& operator=(const GameServer&) = delete;

    void start();

private:
    Task acceptLoop();
    Task clientSession(int clientFd);

    Reactor& reactor_;
    GameWorld world_;
    std::string host_;
    int port_ = 0;
    int listenFd_ = -1;
};
