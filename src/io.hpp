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

#define INAVJAGA_PLAYER_ID_IGNORE 65535
#define INAVJAGA_CHAR_MOVE_IGNORE '_'

struct MoveEvent {
    player_id_t playerId;
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
protected:
    int socketfd;
public:
    int recvRandomSeed();
    void sendRandomSeed(int);

    std::map<std::string, std::variant<int, float>> recvConstants();
    bool sendConstants();

    MoveEvent recvMove();
    void sendMove(MoveEvent);

    static std::pair<size_t, MoveEvent> pollMany(const std::vector<std::shared_ptr<InavjagaGSPIO>>&, int);
};

/**
 * @note receives OWN_ACT, sends ACT
 */
class ServerInavjagaGSPIO: public InavjagaGSPIO {
protected:
    void acceptConnection(int);
public:
    ServerInavjagaGSPIO(int);
    sista::Coordinates negotiateCoordinates(std::weak_ptr<sista::SwappableField>) const;
    void sendPlayers(std::vector<std::shared_ptr<Player>>&, player_id_t);
    bool recvReady();
};

/**
 * @note receives ACT, sends OWN_ACT
 */
class ClientInavjagaGSPIO: public InavjagaGSPIO {
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
    TCPServerInavjagaGSPIO(int);
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
class ServerLocalInavjagaIO: public LocalInavjagaIO {
private:
    std::shared_ptr<std::mutex> writeToChannelsMutex;
    std::vector<std::shared_ptr<ServerInavjagaGSPIO>> neighbors;
public:
    ServerLocalInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>&);
    void sendMove(MoveEvent) override;
};

/** @brief Reads the local moves and communicates them to the server
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class ClientLocalInavjagaIO: public LocalInavjagaIO {
private:
    std::shared_ptr<ClientInavjagaGSPIO> server;
public:
    ClientLocalInavjagaIO(std::shared_ptr<ClientInavjagaGSPIO>);
    void sendMove(MoveEvent) override;
};

/**
 * Communicates over InavjagaGSP in both directions
 */
class RemoteInavjagaIO: public InavjagaIO {
protected:
    std::vector<std::shared_ptr<InavjagaGSPIO>> neighbors;
public:
    MoveEvent getMove() override;
    void sendMove(MoveEvent) override;
};

class ServerRemoteInavjagaIO: public RemoteInavjagaIO {
public:
    ServerRemoteInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>&);
};
class ClientRemoteInavjagaIO: public RemoteInavjagaIO {
public:
    ClientRemoteInavjagaIO(std::shared_ptr<ClientInavjagaGSPIO>);
};
