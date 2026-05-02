#pragma once

#include "player.hpp"
#include <map>
#include <mutex>
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

/** InavjagaGSP input/output
 * It is the abstraction layer between the struct MoveEvent to be sent/received and a socket
 * @note the socket is typically TCP, but subclasses can implement it to be protocol agnostic
 */
class InavjagaGSPIO {
private:
    void sendNo();
    void sendYes();
    sista::Coordinates recvCoordinates();
    void sendCoordinates(sista::Coordinates);
    /**
     * Confirms the coordinates and sends the Player ID to the client
     */
    void leaseCoordinates(sista::Coordinates, int);

public:
    int recvRandomSeed();
    void sendRandomSeed(int);

    std::map<std::string, std::variant<int, float>> recvConstants();
    bool sendConstants();

    MoveEvent recvAct();
    void sendAct(MoveEvent);
};

/**
 * @note receives OWN_ACT, sends ACT
 */
class ServerInavjagaGSPIO: public InavjagaGSPIO {
private:
    int connection;
public:
    void acceptConnection(int);
    sista::Coordinates negotiateCoordinates(std::weak_ptr<sista::SwappableField>) const;
    void sendPlayers(std::vector<std::shared_ptr<Player>>&, player_id_t);
    bool recvReady();
};

/**
 * @note receives ACT, sends OWN_ACT
 */
class ClientInavjagaGSPIO: public InavjagaGSPIO {
private:
    int socketfd;
public:
    ClientInavjagaGSPIO(int, sockaddr*);
    sista::Coordinates negotiateCoordinates() const;
    std::vector<std::shared_ptr<Player>> recvPlayers();
    void sendReady();
};

/**
 * @note it works over TCP
 */
class TCPServerInavjagaGSPIO: public ServerInavjagaGSPIO {
public:
    TCPServerInavjagaGSPIO();
};

/**
 * @note it works over TCP
 */
class TCPClientInavjagaGSPIO: public ClientInavjagaGSPIO {
public:
    TCPClientInavjagaGSPIO(int, sockaddr_in*);
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
class LocalInavjagaIO: public InavjagaIO {
public:
    MoveEvent getMove() override;
};

/** @brief Reads the local moves and communicates them to all clients
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class ServerLocalInavjagaIO: public InavjagaIO {
private:
    std::shared_ptr<std::mutex> writeToChannelsMutex;
    std::vector<std::shared_ptr<InavjagaGSPIO>> neighbors;
public:
    void sendMove(MoveEvent) override;
};

/** @brief Reads the local moves and communicates them to the server
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class ClientLocalInavjagaIO: public InavjagaIO {
private:
    std::unique_ptr<ClientInavjagaGSPIO> server;
public:
    void sendMove(MoveEvent) override;
};

/**
 * Communicates over InavjagaGSP in both directions
 */
class RemoteInavjagaIO: public InavjagaIO {
private:
    std::vector<std::shared_ptr<InavjagaGSPIO>> neighbors;
public:
    MoveEvent getMove() override;
    void sendMove(MoveEvent) override;
};
