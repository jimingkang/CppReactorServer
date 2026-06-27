#pragma once

#include "coro_reactor.h"
#include "coro_task.h"
#include "supermario/super_mario_game_world.h"

#include <string>

class CoroGameServer {
public:
    CoroGameServer(CoroReactor& reactor, std::string host, int port);
    ~CoroGameServer();

    CoroGameServer(const CoroGameServer&) = delete;
    CoroGameServer& operator=(const CoroGameServer&) = delete;

    void start();

private:
    DetachedTask acceptLoop();
    DetachedTask clientLoop(int clientFd);
    DetachedTask writeAll(int fd, std::string data);

    CoroReactor& reactor_;
    supermario::SuperMarioGameWorld world_;
    std::string host_;
    int port_ = 0;
    int listenFd_ = -1;
};
