#include "../include/cross_platform.hpp"
#include "tcp.hpp"
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <string.h>
#include <iostream>
#include <poll.h>
#include <future>
#include <chrono>

extern std::mutex stderrMutex;

std::unique_ptr<ClientInavjagaGSPIO> connectClientToServer(int sockfd, char* addr, char* portno) {
    // https://www.unixguide.net/network/socketfaq/2.16.shtml
    int flag = 1;
    int rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
    if (rc < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Setting the TCP_NODELAY to disable Nagle's algorithm failed" << std::endl;
    }
    struct sockaddr_in serverAddress;
    bzero((char*)&serverAddress, sizeof(serverAddress)); // Clearing
    inet_pton(AF_INET, addr, &(serverAddress.sin_addr)); // https://stackoverflow.com/a/5328184/15888601
    // inet_aton(AF_INET, addr, &(serverAddress.sin_addr)); // man inet_pton (does it accept less than 3 digits?)
    serverAddress.sin_port = htons(atoi(portno));
    serverAddress.sin_family = AF_INET;
    if (int rc = connect(sockfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Could not connect to " << serverAddress.sin_addr.s_addr << ":" << serverAddress.sin_port << '\n';
        std::cerr << "\tError was " << rc << " (" << errno << ")" << std::endl;
    }
    #if DEBUG
    std::unique_lock lock(stderrMutex);
    std::cerr << "Connected successfully to the port" << std::endl;
    #endif
    return std::make_unique<ClientInavjagaGSPIO>(sockfd);
}

void bindServerSocketToPort(int sockfd, char* addr, char* portno) {
    struct sockaddr_in serverAddress;
    bzero((char*)&serverAddress, sizeof(serverAddress)); // Clearing
    serverAddress.sin_family = AF_INET;
    // serverAddress.sin_addr.s_addr = INADDR_ANY; // Accept any incoming address
    inet_pton(AF_INET, addr, &(serverAddress.sin_addr)); // https://stackoverflow.com/a/5328184/15888601
    serverAddress.sin_port = htons(atoi(portno));
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Preparing to listen on " << serverAddress.sin_addr.s_addr << ":" << serverAddress.sin_port << std::endl;
    }
    if (int rc = bind(sockfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)) < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Could not bind address " << addr << ":" << portno << ", error code " << rc << "(" << errno << ")" << std::endl;
    }
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Bound the socket to the port successfully" << std::endl;
    }
    #endif
    errno = 0;
    if (listen(sockfd, 10) < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Could not listen on the specified port, failed with " << errno << "=>" << strerror(errno) << std::endl;
    }
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Listening to the port successfully" << std::endl;
    }
    #endif
}

std::vector<std::shared_ptr<ServerInavjagaGSPIO>> waitForConnections(int movesockfd, int syncsockfd) {
    std::vector<std::shared_ptr<ServerInavjagaGSPIO>> collectedConnections = {nullptr};
    std::future stopLobbySignal = std::async(std::launch::async, getch);
    std::chrono::duration zeroTime = std::chrono::microseconds(0);
    while (true) {
        if (stopLobbySignal.wait_for(zeroTime) == std::future_status::ready) {
            char input_ = stopLobbySignal.get();
            if (input_ == 'n' || input_ == 'N') {
                break;
            } else {
                stopLobbySignal = std::async(std::launch::async, getch);
            }
        }
        if (awaitConnection(movesockfd, 100)) {
            std::unique_ptr<ServerInavjagaGSPIO> gspio = std::make_unique<TCPServerInavjagaGSPIO>();
            gspio->acceptMoveConnection(movesockfd);
            if (!awaitConnection(syncsockfd)) {
                std::unique_lock lock(stderrMutex);
                std::cerr << "Could not open the connection for synchronization in time on " << syncsockfd << std::endl;
                continue;
            }
            gspio->acceptSyncConnection(syncsockfd);
            collectedConnections.push_back(std::move(gspio));
        }
    }
    return collectedConnections;
}

/** @brief Awaits a connection on the specified socket
 * @param sockfd The socket file descriptor
 * @param timeout The timeout after which to return false
 * @retval true When the connection happened
 * @retval false When the connection timed out
 * @return Whether the connection happened
 */
bool awaitConnection(int sockfd, int timeout) {
    struct pollfd pollFd = {0,0,0};
    pollFd.fd = sockfd;
    pollFd.events = POLLIN;
    if (poll(&pollFd, 1, timeout) < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Error polling socket " << sockfd << std::endl;
    }
    return pollFd.revents & POLLIN;
}
