#pragma once

#include "io.hpp"

std::unique_ptr<ClientInavjagaGSPIO> connectClientToServer(int, int, char*, char*, char*);
void bindServerSocketToPort(int, char*, char*);
std::vector<std::shared_ptr<ServerInavjagaGSPIO>> waitForConnections(int, int);
bool awaitConnection(int, int timeout=1000);
