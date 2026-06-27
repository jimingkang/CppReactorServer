#pragma once

namespace supermario {

struct Player {
    int id = 0;
    int x = 0;
    int y = 0;
    int hp = 100;
    int score = 0;
};

struct WorldCoin {
    int id = 0;
    int x = 0;
    int y = 0;
    bool collected = false;
};

struct WorldMonster {
    int id = 0;
    int x = 0;
    int y = 0;
    int minX = 0;
    int maxX = 0;
    int speed = 0;
};

} // namespace supermario
