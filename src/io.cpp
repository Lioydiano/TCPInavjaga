#include "../include/cross_platform.hpp"
#include "io.hpp"
#include <iostream>

#define INAVJAGA_PLAYER_ID_IGNORE 0

MoveEvent LocalInavjagaIO::getMove() {
    return {INAVJAGA_PLAYER_ID_IGNORE, getch()};
}

/** @brief Waits for a move event and returns it
 * @note polls asynchronously all the connections it owns
 * @note if it is the server, then it will poll multiple ones
 * @return a move event representing the received move
 */
MoveEvent RemoteInavjagaIO::getMove() {

}

ClientInavjagaGSPIO::ClientInavjagaGSPIO(int sockfd, sockaddr* srvaddr) {
    if (connect(sockfd, (struct sockaddr*)&srvaddr, sizeof(srvaddr)) < 0) {
        std::cerr << "Could not connect to " << srvaddr->sa_data;
    }
    this->socketfd = sockfd;
}
TCPClientInavjagaGSPIO::TCPClientInavjagaGSPIO(int sockfd, sockaddr_in* srvaddr): ClientInavjagaGSPIO(sockfd, (sockaddr*)srvaddr) {}

void ServerInavjagaGSPIO::acceptConnection(int sockfd) {
    sockaddr clientAddress;
    socklen_t length = sizeof(clientAddress);
    this->connection = accept(sockfd, &clientAddress, &length);
    if (this->connection < 0) {
        std::cerr << "Something went wrong with accepting the connection from " << clientAddress.sa_data << std::endl;
    }
}

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

ServerRemoteInavjagaIO::ServerRemoteInavjagaIO(std::vector<std::shared_ptr<ServerInavjagaGSPIO>>& connectionsToClients) {
    this->neighbors = {};
    for (std::shared_ptr<ServerInavjagaGSPIO> connection : connectionsToClients) {
        // https://stackoverflow.com/a/43682576/15888601
        this->neighbors.push_back(std::static_pointer_cast<InavjagaGSPIO>(connection));
    }
}

ClientRemoteInavjagaIO::ClientRemoteInavjagaIO(std::shared_ptr<ClientInavjagaGSPIO> connectionToServer) {
    // https://stackoverflow.com/a/43682576/15888601
    this->neighbors = {std::static_pointer_cast<InavjagaGSPIO>(connectionToServer)};
}
