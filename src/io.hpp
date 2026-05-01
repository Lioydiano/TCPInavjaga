#pragma once

#include "player.hpp"
#include <map>
#include <string>
#include <memory>
#include <vector>
#include <sista/coordinates.hpp>
#include <sys/types.h>
#include <netinet/ip.h>

struct MoveEvent {
    unsigned short playerId;
    char move;
};

/**
 * Provides an abstraction over the input sources
 */
class InavjagaIO {
public:
    virtual MoveEvent getMove();
    virtual void sendMove(MoveEvent);
};

/** @brief Reads the local moves and communicates them
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class LocalInavjagaIO: public InavjagaIO {};

/** @brief Reads the local moves and communicates them to all clients
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class ServerLocalInavjagaIO: public InavjagaIO {
public:
    MoveEvent getMove() override;
    void sendMove(MoveEvent) override;
};

/** @brief Reads the local moves and communicates them to the server
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class ClientLocalInavjagaIO: public InavjagaIO {
private:
    std::unique_ptr<InavjagaClientGSPIO> server;
public:
    MoveEvent getMove() override;
    void sendMove(MoveEvent) override;
};

/**
 * Communicates over InavjagaGSP in both directions
 */
class RemoteInavjagaIO: public InavjagaIO {
private:
    std::vector<std::unique_ptr<InavjagaGSPIO>> neighbors;
public:
    MoveEvent getMove() override;
    void sendMove(MoveEvent) override;
};

/** InavjagaGSP input/output
 * It is the abstraction layer between the struct MoveEvent to be sent/received and a socket
 * @note the socket is typically TCP, but subclasses can implement it to be protocol agnostic
 */
class InavjagaGSPIO {
private:
    sista::Coordinates recvCoordinates();
    void sendCoordinates(sista::Coordinates);
    /**
     * Confirms the coordinates and sends the Player ID to the client
     */
    void leaseCoordinates(sista::Coordinates, int);

public:
    int recvRandomSeed();
    void sendRandomSeed(int);

    std::map<std::string, std::string> recvConstants();
    void sendConstants();

    virtual sista::Coordinates negotiateCoordinates();

    /**
     * @note specify the ID of the user so that it is not included in the vector
     */
    std::vector<std::shared_ptr<Player>> recvPlayers(int);
    void sendPlayers();

    void sendReady();

    MoveEvent recvAct();
    void sendAct(MoveEvent);
};

/**
 * @note receives OWN_ACT, sends ACT
 */
class InavjagaServerGSPIO: public InavjagaGSPIO {
    sista::Coordinates negotiateCoordinates() override;
};

/**
 * @note receives ACT, sends OWN_ACT
 */
class InavjagaClientGSPIO: public InavjagaGSPIO {
    sista::Coordinates negotiateCoordinates() override;
};
