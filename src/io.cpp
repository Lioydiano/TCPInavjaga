#include "include/cross_platform.hpp"
#include "io.hpp"
#include <iostream>

#define INAVJAGA_PLAYER_ID_IGNORE 0

MoveEvent LocalInavjagaIO::getMove() {
    return {INAVJAGA_PLAYER_ID_IGNORE, getch()};
}

/** @brief Waits for a move event and returns it
 * @note polls asynchronously all the sockets it owns
 * @note if it is the server, then it will poll multiple ones
 * @return a move event representing the received move
 */
MoveEvent RemoteInavjagaIO::getMove() {

}

ClientInavjagaGSPIO::ClientInavjagaGSPIO(int sockfd, sockaddr* srvaddr) {
    if (connect(sockfd, (struct sockaddr*)&srvaddr, sizeof(srvaddr)) < 0) {
        std::cerr << "Could not connect to " << srvaddr->sa_data;
    }
}
TCPClientInavjagaGSPIO::TCPClientInavjagaGSPIO(int sockfd, sockaddr_in* srvaddr): ClientInavjagaGSPIO(sockfd, (sockaddr*)srvaddr) {}
