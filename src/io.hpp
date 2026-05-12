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
#include <shared_mutex>

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
    /**
     * Confirms the coordinates and sends the Player ID to the client
     */
    void leaseCoordinates(sista::Coordinates, int);
    static struct pollfd pollFds[10];
protected:
    static std::shared_mutex outputMutex;
    static const char acceptMessage[2];
    static const char yesMessage[2];
    static const char noMessage[2];
    static const char constantsTermination[3];
    int socketfd;

    bool recvBool(int timeout = 1000) const;
    void sendCoordinates(const sista::Coordinates& coordinates) const;
public:
    void sendNo();
    void sendYes();
    bool waitYes(int timeout=1000);

    uint32_t recvRandomSeed(int timeout=10000);
    void sendRandomSeed(uint32_t);

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
    bool offerCoordinates(const sista::Coordinates&) const;
    void sendPlayers(std::vector<std::shared_ptr<Player>>&, player_id_t);
    bool recvReady(int timeout=1000);
};

/**
 * @note receives ACT, sends OWN_ACT
 */
class ClientInavjagaGSPIO: public InavjagaGSPIO {
public:
    ClientInavjagaGSPIO(int);
    sista::Coordinates recvCoordinates(int timeout=3000) const;
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
    TCPClientInavjagaGSPIO(int);
};

/**
 * Provides an abstraction over the input sources
 */
class InavjagaIO {
protected:
    InavjagaIO();
public:
    virtual ~InavjagaIO();
    virtual MoveEvent getMove(int timeout=3000) = 0; // https://stackoverflow.com/a/9260274/15888601
    virtual void sendMove(MoveEvent) = 0;
};

/** @brief Reads the local moves and communicates them
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class LocalInavjagaIO: public InavjagaIO {
protected:
    LocalInavjagaIO();
public:
    MoveEvent getMove(int timeout=3000) override;
};

/** @brief Reads the local moves and communicates them to all clients
 * @note takes input from stdin
 * @note writes output as ACT with InavjagaGSP
 */
class ServerLocalInavjagaIO: public LocalInavjagaIO {
private:
    std::shared_ptr<std::mutex> writeToChannelsMutex;
    std::vector<std::shared_ptr<ServerInavjagaGSPIO>> neighbors;
protected:
    ServerLocalInavjagaIO();
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
protected:
    ClientLocalInavjagaIO();
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
    RemoteInavjagaIO();
public:
    RemoteInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>&);
    RemoteInavjagaIO(std::initializer_list<std::shared_ptr<ClientInavjagaGSPIO>>);
    MoveEvent getMove(int timeout = 3000) override; // https://stackoverflow.com/a/9260274/15888601
    void sendMove(MoveEvent) override;
};

class ServerRemoteInavjagaIO: public RemoteInavjagaIO {
protected:
    ServerRemoteInavjagaIO();
public:
    ServerRemoteInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>&);
};
class ClientRemoteInavjagaIO: public RemoteInavjagaIO {
protected:
    ClientRemoteInavjagaIO();
public:
    ClientRemoteInavjagaIO(std::shared_ptr<ClientInavjagaGSPIO>);
};
