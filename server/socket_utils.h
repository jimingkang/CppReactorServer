#pragma once

#include <string>

int setNonBlocking(int fd);
int createListenSocket(const std::string& host, int port, int backlog = 256);
