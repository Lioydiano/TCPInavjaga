#include "../include/cross_platform.hpp"
#include "io.hpp"
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <exception>
#include <string.h>
#include <iostream>
#include <future>
#include <chrono>
#include <poll.h>

int InavjagaGSPIO::recvRandomSeed() {
    // https://stackoverflow.com/a/64357776/15888601
    int32_t seed;
    read(socketfd, &seed, sizeof(int32_t));
    return ntohl(seed);
}

void InavjagaGSPIO::sendRandomSeed(int seed) {
    // https://stackoverflow.com/a/64357776/15888601
    int32_t converted = htonl(seed);
    write(socketfd, &converted, sizeof(converted));
}

/** @brief Waits for a message from the other end of the channel
 * @throws std::runtime_error when the recv call on the file descriptor fails
 * @return A move event representing the received move
 */
MoveEvent InavjagaGSPIO::recvMove() {
    static char buffer[4] = {0};
    /** Messages are in the form "ID;MOVE"
     * @note the ID has a variable length, that we can fix by padding
     * @warning for the moment we are capping it to 9 clients
     * @todo extend it to more users by padding
     * @note every move is made of one character
     */
    // https://stackoverflow.com/questions/71744538/why-would-one-need-to-use-msg-waitall-flag-instead-of-0-flag-why-to-use-it
    int rc = recv(this->socketfd, &buffer, 1+1+1, MSG_WAITALL);
    MoveEvent moveEvent = {INAVJAGA_PLAYER_ID_IGNORE, INAVJAGA_CHAR_MOVE_IGNORE};
    if (rc < 0) {
        std::string errorBuffer = "Scanning a socket that was expected to be empty gave error code " + std::to_string(rc);
        throw new std::runtime_error(errorBuffer);
    }
    sscanf(buffer, "%hu;%c", &moveEvent.playerId, &moveEvent.move);
    return moveEvent;
}

/** @brief Sends a message to the other end of the channel
 * @param moveEvent The move event to transmit
 */
void InavjagaGSPIO::sendMove(MoveEvent moveEvent) {
    static char buffer[4] = {0};
    snprintf(buffer, 4, "%hu;%c", moveEvent.playerId, moveEvent.move);
    send(socketfd, buffer, 4, 0);
}

std::vector<struct pollfd> InavjagaGSPIO::pollFds = {};

/** @brief Polls the InavjagaGSP connections and returns the first one to return
 * @note For the moment we accept at most 9 ios, our cap to the number of clients
 * @warning For the moment we trust the clients to not lie about their Player ID
 * @param ios The input sources to poll for move events
 * @param timeout The time expressed in milliseconds for which to poll for moves
 *                after which the method just returns an empty move
 * @retval {INAVJAGA_PLAYER_ID_IGNORE,INAVJAGA_CHAR_MOVE_IGNORE} when no input
 * @returns The move event and the index of the input source in the given vector
 */
std::pair<size_t, MoveEvent> InavjagaGSPIO::pollMany(
    const std::vector<std::shared_ptr<InavjagaGSPIO>>& ios, int timeout = 1000) {
        const size_t iosLen = ios.size();
        pollFds.resize(iosLen);
        for (size_t i = 0; i < iosLen; i++) {
            pollFds[i].fd = ios[i]->socketfd;
            pollFds[i].events = POLLIN;
        }
        int rc = poll(pollFds.data(), iosLen, timeout);
        if (rc <= 0) {
            if (rc < 0) {
                std::cerr << "poll() failed with code " << rc << std::endl;
                /** @note We should definitely log this,
                 * but it can fail without throwing for now,
                 * we just don't need it outside debugging
                 */
            } // else it just timed out (https://en.ittrip.xyz/c-language/c-timeout-handling)
            return std::make_pair(
                INAVJAGA_PLAYER_ID_IGNORE,
                MoveEvent{
                    INAVJAGA_PLAYER_ID_IGNORE,
                    INAVJAGA_CHAR_MOVE_IGNORE
                }
            );
        }
        for (size_t i = 0; i < iosLen; i++) {
            if (pollFds[i].revents & POLLHUP) { // If the client disconnected...
                return std::make_pair(i, MoveEvent{(player_id_t)i, 'Q'});
            } else if (pollFds[i].revents & POLLIN) { // If the client sent something...
                try {
                    MoveEvent moveEvent = ios[i]->recvMove();
                    return std::make_pair(i, moveEvent);
                } catch (std::exception& e) {
                    std::cerr << e.what() << std::endl;
                    /** @note We should definitely log this,
                     * but it can fail without throwing for now,
                     * we just don't need it outside debugging
                     */
                }
            }
        }
        return std::make_pair(
            INAVJAGA_PLAYER_ID_IGNORE,
            MoveEvent{
                INAVJAGA_PLAYER_ID_IGNORE,
                INAVJAGA_CHAR_MOVE_IGNORE
            }
        );
}

/** Returns a move event by the local player
 * @param timeout The time expressed in milliseconds for which to wait for moves
 *                after which the method just returns an empty move
 * @retval {INAVJAGA_PLAYER_ID_IGNORE,INAVJAGA_CHAR_MOVE_IGNORE} when no input
 * @return A move event received as local input, typically from stdin
 */
MoveEvent LocalInavjagaIO::getMove(int timeout = 3000) {
    std::future<char> input_ = std::async(getch);
    std::future_status status = input_.wait_for(std::chrono::milliseconds(timeout));
    if (status != std::future_status::ready) {
        return {INAVJAGA_PLAYER_ID_IGNORE,INAVJAGA_CHAR_MOVE_IGNORE};
    }
    return {INAVJAGA_PLAYER_ID_IGNORE, input_.get()};
}

/** @brief Waits for a move event and returns it
 * @note polls asynchronously all the connections it owns
 * @note if it is the server, then it will poll multiple ones
 * @param timeout The time expressed in milliseconds for which to wait for moves
 *                after which the method just returns an empty move
 * @return a move event representing the received move
 */
MoveEvent RemoteInavjagaIO::getMove(int timeout = 3000) {
    MoveEvent moveEvent = {INAVJAGA_PLAYER_ID_IGNORE, INAVJAGA_CHAR_MOVE_IGNORE};
    std::chrono::high_resolution_clock timer;
    std::chrono::time_point start = timer.now();
    while (moveEvent.playerId == INAVJAGA_PLAYER_ID_IGNORE) {
        /// @warning This doesn't return the flow for... potentially forever
        try {
            // Now a question is what will happen to the nullptr neighbors
            moveEvent = InavjagaGSPIO::pollMany(this->neighbors).second;
        } catch (std::exception& e) {
            // It technically doesn't throw at the current stage
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(timer.now() - start).count() > timeout) {
            return moveEvent; // If timeout expires, it returns even if no input was received
        }
    }
    return moveEvent;
}

/** @brief Sends a move to all the listening clients
 * @param moveEvent The move event to be sent
 */
void RemoteInavjagaIO::sendMove(MoveEvent moveEvent) {
    for (std::shared_ptr<InavjagaGSPIO> gspio : this->neighbors) {
        if (!gspio) continue;
        gspio->sendMove(moveEvent);
    }
}

ClientInavjagaGSPIO::ClientInavjagaGSPIO(int sockfd, sockaddr* srvaddr) {
    if (connect(sockfd, (struct sockaddr*)&srvaddr, sizeof(srvaddr)) < 0) {
        std::cerr << "Could not connect to " << srvaddr->sa_data;
    }
    this->socketfd = sockfd;
}
TCPClientInavjagaGSPIO::TCPClientInavjagaGSPIO(int sockfd, sockaddr_in* srvaddr): ClientInavjagaGSPIO(sockfd, (sockaddr*)srvaddr) {}

ServerInavjagaGSPIO::ServerInavjagaGSPIO(int sockfd) {
    this->acceptConnection(sockfd);
}
TCPServerInavjagaGSPIO::TCPServerInavjagaGSPIO(int sockfd): ServerInavjagaGSPIO(sockfd) {}

void ServerInavjagaGSPIO::acceptConnection(int sockfd) {
    sockaddr clientAddress;
    socklen_t length = sizeof(clientAddress);
    this->socketfd = accept(sockfd, &clientAddress, &length);
    if (this->socketfd < 0) {
        std::cerr << "Something went wrong with accepting the connection from " << clientAddress.sa_data << std::endl;
    }
}

const char InavjagaGSPIO::acceptMessage[2] = "A";

bool ServerInavjagaGSPIO::recvReady(int timeout) {
    char inputBuffer[2] = {0};
    std::future<ssize_t> input_ = std::async(recv, socketfd, inputBuffer, (size_t)2, 0);
    std::future_status status = input_.wait_for(std::chrono::milliseconds(timeout));
    if (status == std::future_status::ready) {
        int rc = input_.get();
        if (rc < 0) return false;
        /// @todo fix, this is dangerous asf
        if (strcmp(InavjagaGSPIO::acceptMessage, inputBuffer) == 0) {
            return true;
        }
    }
    return false;
}

void ClientInavjagaGSPIO::sendReady() {
    send(socketfd, acceptMessage, 2, 0);
}

InavjagaIO::InavjagaIO() {}
InavjagaIO::~InavjagaIO() {}
LocalInavjagaIO::LocalInavjagaIO(): InavjagaIO::InavjagaIO() {}
ServerLocalInavjagaIO::ServerLocalInavjagaIO(): LocalInavjagaIO::LocalInavjagaIO() {}
ClientLocalInavjagaIO::ClientLocalInavjagaIO(): LocalInavjagaIO::LocalInavjagaIO() {}

/** Constructs ServerLocalInavjagaIO with the connections to its clients.
 * @param connectionsToClients a collection of the connections to clients
 * @warning At this stage the connections to the clients must be ready to transmit and receive moves
 * @warning Some of the connections have already been disconnected and are nullptr
 * @note The reason why the dead connections are kept there is that we want alignment of indices with Player::players[i + 1]
 */
ServerLocalInavjagaIO::ServerLocalInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>& connectionsToClients) {
    // I should check if the copy is how I would expect it to be https://stackoverflow.com/a/11348411/15888601
    this->neighbors = connectionsToClients;
}

/** Constructs ClientLocalInavjagaIO by providing it with a connection to the server.
 * @param connectionToServer the connection from the client to the server
 * @warning At this stage the connection to the server must be ready to transmit and receive moves
 */
ClientLocalInavjagaIO::ClientLocalInavjagaIO(std::shared_ptr<ClientInavjagaGSPIO> connectionToServer) {
    this->server = connectionToServer;
}

RemoteInavjagaIO::RemoteInavjagaIO() {}
RemoteInavjagaIO::RemoteInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>& connections) {
    this->neighbors = {};
    for (std::shared_ptr<ServerInavjagaGSPIO> connection : connections) {
        // https://stackoverflow.com/a/43682576/15888601
        this->neighbors.push_back(std::static_pointer_cast<InavjagaGSPIO>(connection));
    }
}
RemoteInavjagaIO::RemoteInavjagaIO(std::initializer_list<std::shared_ptr<ClientInavjagaGSPIO>> connections) {
    this->neighbors = {};
    for (std::shared_ptr<InavjagaGSPIO> connection : connections) {
        // https://stackoverflow.com/a/43682576/15888601
        this->neighbors.push_back(std::static_pointer_cast<InavjagaGSPIO>(connection));
    }
}

ServerRemoteInavjagaIO::ServerRemoteInavjagaIO() {}
ServerRemoteInavjagaIO::ServerRemoteInavjagaIO(
    std::vector<std::shared_ptr<ServerInavjagaGSPIO>>& connectionsToClients
): RemoteInavjagaIO(connectionsToClients) {}

ClientRemoteInavjagaIO::ClientRemoteInavjagaIO() {}
ClientRemoteInavjagaIO::ClientRemoteInavjagaIO(
    std::shared_ptr<ClientInavjagaGSPIO> connectionToServer
): RemoteInavjagaIO({connectionToServer}) {}
