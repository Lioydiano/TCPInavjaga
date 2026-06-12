#include "../include/cross_platform.hpp"
#include "io.hpp"
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <exception>
#include <string.h>
#include <iostream>
#include <future>
#include <chrono>
#include <poll.h>
#include <errno.h>

std::mutex InavjagaGSPIO::outputMutex = std::mutex();
std::mutex InavjagaGSPIO::syncMutex = std::mutex();
extern std::mutex stderrMutex;

bool MoveEvent::operator<(const MoveEvent& moveEvent) const {
    if (this->playerId < moveEvent.playerId) return true;
    return this->move < moveEvent.move;
}

bool writeAll(int sockfd, const void* buffer, size_t size) {
    size_t total = 0;
    while (total < size) {
        int written = write(sockfd, buffer + total, size - total);
        if (written < 0) {
            {
                std::unique_lock lock(stderrMutex);
                std::cerr << "Writing " << size << " bytes to "
                        << sockfd << " failed with " << errno << std::endl;
            }
            return false;
        } else if (written > 0) {
            total += (size_t)written;
        } else { // Didn't write anything but no error was detected
            return false;
        }
    }
    return true;
}

void disableNagle(int sockfd) {
    // https://www.unixguide.net/network/socketfaq/2.16.shtml
    int flag = 1;
    int rc = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(int));
    if (rc < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Setting the TCP_NODELAY to disable Nagle's algorithm failed" << std::endl;
    }
}

inline bool isSocketAlive(int descriptor) {
    // Source - https://stackoverflow.com/a/4142038
    // Posted by Simone, modified by community. See post 'Timeline' for change history
    // Retrieved 2026-05-15, License - CC BY-SA 3.0

    int error = 0;
    socklen_t len = sizeof (error);
    int retval = getsockopt(descriptor, SOL_SOCKET, SO_ERROR, &error, &len);

    if (retval != 0) {
        /* there was a problem getting the error code */
        std::unique_lock lock(stderrMutex);
        fprintf(stderr, "error getting socket error code: %s\n", strerror(retval));
        return false;
    }
    if (error != 0) {
        /* socket has a non zero error status */
        std::unique_lock lock(stderrMutex);
        std::cerr << "isSocketAlive - socket error: " << strerror(error);
        fprintf(stderr, "\tsocket error: %s\n", strerror(error));
        return false;
    }
    return true;
}

bool InavjagaGSPIO::isConnectionAlive() {
    return isSocketAlive(this->socketfd);
}

bool ClientRemoteInavjagaIO::isChannelAlive() {
    return this->neighbors[1]->isConnectionAlive();
}

/** @brief Receives the random seed from the server
 * @throws std::runtime_error When polling the socket failed
 * @throws std::runtime_error When no message is ready yet
 */
uint32_t InavjagaGSPIO::recvRandomSeed(int timeout) {
    // https://stackoverflow.com/a/64357776/15888601
    uint32_t seed;
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Reading random seed" << std::endl;
    }
    #endif
    struct pollfd pollFd_ = {0,0,0};
    pollFd_.fd = this->socketfd;
    pollFd_.events = POLLIN;
    if (int rc = poll(&pollFd_, 1, timeout); rc < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
        throw std::runtime_error("Polling failed");
    }
    if (pollFd_.revents & POLLIN) {
        read(socketfd, &seed, sizeof(uint32_t));
        return ntohl(seed);
    }
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "No message ready yet" << std::endl;
    }
    throw std::runtime_error("No message ready yet");
}

void InavjagaGSPIO::sendRandomSeed(uint32_t seed) {
    // https://stackoverflow.com/a/64357776/15888601
    uint32_t converted = htonl(seed);
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << converted << " is our integer and its size is " << sizeof(uint32_t) << std::endl;
    }
    std::unique_lock lock(outputMutex);
    writeAll(this->socketfd, &converted, sizeof(uint32_t));
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
    int rc = recv(this->socketfd, &buffer, 1+1+1+1, MSG_WAITALL);
    MoveEvent moveEvent = {INAVJAGA_PLAYER_ID_IGNORE, INAVJAGA_CHAR_MOVE_IGNORE};
    if (rc < 0) {
        std::string errorBuffer = "Scanning a socket that was expected to be empty gave error code " + std::to_string(rc);
        throw std::runtime_error(errorBuffer);
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
    std::unique_lock lock(outputMutex);
    writeAll(socketfd, buffer, 4);
}

struct pollfd InavjagaGSPIO::pollFds[10] = {{0,0,0}};

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
        for (size_t i = 1; i < iosLen; i++) {
            pollFds[i].fd = ios[i]->socketfd;
            pollFds[i].events = POLLIN;
        }
        errno = 0;
        int rc = poll(&(pollFds[1]), iosLen - 1, timeout);
        #if DEBUG
        // std::cerr << &(pollFds[1]) << " for a __nfds=" << iosLen - 1 << std::endl;
        #endif
        if (rc <= 0) {
            if (rc < 0) {
                std::unique_lock lock(stderrMutex);
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
        for (size_t i = 1; i < iosLen; i++) {
            if (pollFds[i].revents & POLLHUP) { // If the client disconnected...
                return std::make_pair(i, MoveEvent{(player_id_t)i, 'Q'});
            } else if (pollFds[i].revents & POLLIN) { // If the client sent something...
                try {
                    MoveEvent moveEvent = ios[i]->recvMove();
                    return std::make_pair(i, moveEvent);
                } catch (std::exception& e) {
                    std::unique_lock lock(stderrMutex);
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
MoveEvent LocalInavjagaIO::getMove(int timeout) {
    std::future<char> input_ = std::async(getch);
    std::future_status status = input_.wait_for(std::chrono::milliseconds(timeout));
    if (status != std::future_status::ready) {
        return {INAVJAGA_PLAYER_ID_IGNORE,INAVJAGA_CHAR_MOVE_IGNORE};
    }
    return {INAVJAGA_PLAYER_ID_IGNORE, input_.get()};
}

/** @brief Sends a move event to the server
 * @param moveEvent The move event to send
 */
void ClientLocalInavjagaIO::sendMove(MoveEvent moveEvent) {
    this->server->sendMove(moveEvent);
}

/** @brief Sends a move event to every client
 * @param moveEvent The move event to send
 */
void ServerLocalInavjagaIO::sendMove(MoveEvent moveEvent) {
    for (std::shared_ptr<InavjagaGSPIO> gspio : this->neighbors) {
        if (!gspio) continue;
        gspio->sendMove(moveEvent);
    }
}

/** @brief Waits for a move event and returns it
 * @note polls asynchronously all the connections it owns
 * @note if it is the server, then it will poll multiple ones
 * @param timeout The time expressed in milliseconds for which to wait for moves
 *                after which the method just returns an empty move
 * @return a move event representing the received move
 */
MoveEvent RemoteInavjagaIO::getMove(int timeout) {
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
    for (size_t i = 1; i < this->neighbors.size(); i++) {
        if (!this->neighbors[i]) continue;
        if (i != moveEvent.playerId) {
            this->neighbors[i]->sendMove(moveEvent);
        }
    }
}

ClientInavjagaGSPIO::ClientInavjagaGSPIO() {}

void ClientInavjagaGSPIO::connectSocket(int sockfd, char* addr, char* portno) {
    disableNagle(sockfd);
    struct sockaddr_in serverAddress;
    bzero((char*)&serverAddress, sizeof(serverAddress)); // Clearing
    inet_pton(AF_INET, addr, &(serverAddress.sin_addr)); // https://stackoverflow.com/a/5328184/15888601
    // inet_aton(AF_INET, addr, &(serverAddress.sin_addr)); // man inet_pton (does it accept less than 3 digits?)
    serverAddress.sin_port = htons(atoi(portno));
    serverAddress.sin_family = AF_INET;
    if (int rc = connect(sockfd, (struct sockaddr*)&serverAddress, sizeof(serverAddress)); rc < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Could not connect to " << serverAddress.sin_addr.s_addr << ":" << serverAddress.sin_port << '\n';
        std::cerr << "\tError was " << rc << " (" << errno << ")" << std::endl;
    }
    #if DEBUG
    std::unique_lock lock(stderrMutex);
    std::cerr << "Connected successfully to the port" << std::endl;
    #endif
}

void ClientInavjagaGSPIO::connectMove(int sockfd, char* addr, char* portno) {
    this->connectSocket(sockfd, addr, portno);
    this->socketfd = sockfd;
}

void ClientInavjagaGSPIO::connectSync(int sockfd, char* addr, char* portno) {
    this->connectSocket(sockfd, addr, portno);
    this->syncsocketfd = sockfd;
}

ServerInavjagaGSPIO::ServerInavjagaGSPIO() {}

void ServerInavjagaGSPIO::acceptSyncConnection(int sockfd) {
    this->syncsocketfd = acceptConnection(sockfd);
}

void ServerInavjagaGSPIO::acceptMoveConnection(int sockfd) {
    this->socketfd = acceptConnection(sockfd);
}

int ServerInavjagaGSPIO::acceptConnection(int sockfd) {
    sockaddr clientAddress;
    socklen_t length = sizeof(clientAddress);
    int newSocket = accept(sockfd, &clientAddress, &length);
    if (newSocket < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Something went wrong with accepting the connection from " << clientAddress.sa_data << std::endl;
    }
    #if DEBUG
    std::unique_lock lock(stderrMutex);
    std::cerr << "Accepted connection from " << clientAddress.sa_data << std::endl;
    #endif
    return newSocket;
}

const char InavjagaGSPIO::acceptMessage[2] = "A";
const char InavjagaGSPIO::yesMessage[2] = "y";
const char InavjagaGSPIO::noMessage[2] = "n";
const char InavjagaGSPIO::constantsTermination[3] = "-:";

/** @brief Sends the constants needed for the game from the server to the client
 * @warning This is only returning true, it does not have error handling yet
 * @todo Add error handling from the return codes of send
 * @todo Divide the creation of one big buffer from the sending of the same one
 *       so we can reuse the same buffer for multiple clients and also not waste segments
 * @note From Wireshark captures it turns out that the constants
 *       are all sent in the same segment
 * @return Whether the sending was successful
 */
bool InavjagaGSPIO::sendConstants() {
    std::string buffer;
    std::unique_lock lock(outputMutex);
    buffer.append("WIDTH:" + std::to_string(WIDTH) + ";");
    buffer.append("HEIGHT:" + std::to_string(HEIGHT) + ";");
    buffer.append("TUNNEL_UNIT:" + std::to_string(TUNNEL_UNIT) + ";");
    buffer.append("PORTALS_PER_LINE:" + std::to_string(PORTALS_PER_LINE) + ";");
    buffer.append("FRAME_DURATION:" + std::to_string(FRAME_DURATION) + ";");
    buffer.append("BULLET_SPEED:" + std::to_string(BULLET_SPEED) + ";");
    buffer.append("DROP_INVENTORY_ON_DEATH:" + std::to_string(DROP_INVENTORY_ON_DEATH) + ";");
    buffer.append("INITIAL_CLAY:" + std::to_string(INITIAL_CLAY) + ";");
    buffer.append("INITIAL_BULLETS:" + std::to_string(INITIAL_BULLETS) + ";");
    buffer.append("INITIAL_MEAT:" + std::to_string(INITIAL_MEAT) + ";");
    buffer.append("LOOT_ARCHER_CLAY:" + std::to_string(LOOT_ARCHER_CLAY) + ";");
    buffer.append("LOOT_ARCHER_BULLETS:" + std::to_string(LOOT_ARCHER_BULLETS) + ";");
    buffer.append("LOOT_ARCHER_MEAT:" + std::to_string(LOOT_ARCHER_MEAT) + ";");
    buffer.append("LOOT_WORM_HEAD_CLAY:" + std::to_string(LOOT_WORM_HEAD_CLAY) + ";");
    buffer.append("LOOT_WORM_HEAD_BULLETS:" + std::to_string(LOOT_WORM_HEAD_BULLETS) + ";");
    buffer.append("LOOT_WORM_HEAD_MEAT:" + std::to_string(LOOT_WORM_HEAD_MEAT) + ";");
    buffer.append("COST_OF_MINE_CLAY:" + std::to_string(COST_OF_MINE_CLAY) + ";");
    buffer.append("COST_OF_MINE_BULLETS:" + std::to_string(COST_OF_MINE_BULLETS) + ";");
    buffer.append("COST_OF_MINE_MEAT:" + std::to_string(COST_OF_MINE_MEAT) + ";");
    buffer.append("MEAT_DURATION_PERIOD:" + std::to_string(MEAT_DURATION_PERIOD) + ";");
    buffer.append("SPAWN_COORDINATES_Y:" + std::to_string(SPAWN_COORDINATES_Y) + ";");
    buffer.append("SPAWN_COORDINATES_X:" + std::to_string(SPAWN_COORDINATES_X) + ";");
    buffer.append("RESPAWN_COORDINATES_Y:" + std::to_string(RESPAWN_COORDINATES_Y) + ";");
    buffer.append("RESPAWN_COORDINATES_X:" + std::to_string(RESPAWN_COORDINATES_X) + ";");
    buffer.append("MINE_MINIMUM_DAMAGE:" + std::to_string(MINE_MINIMUM_DAMAGE) + ";");
    buffer.append("MINE_MAXIMUM_DAMAGE:" + std::to_string(MINE_MAXIMUM_DAMAGE) + ";");
    buffer.append("MINE_SENSITIVITY_RADIUS:" + std::to_string(MINE_SENSITIVITY_RADIUS) + ";");
    buffer.append("MINE_DAMAGE_RADIUS:" + std::to_string(MINE_DAMAGE_RADIUS) + ";");
    buffer.append("INITIAL_WALL_STRENGTH:" + std::to_string(INITIAL_WALL_STRENGTH) + ";");
    buffer.append("WORM_HEALTH_POINTS:" + std::to_string(WORM_HEALTH_POINTS) + ";");
    buffer.append("WALL_WEARING_PROBABILITY:" + std::to_string(WALL_WEARING_PROBABILITY) + ";");
    buffer.append("DAMAGED_WALLS_COUNT:" + std::to_string(DAMAGED_WALLS_COUNT) + ";");
    buffer.append("MINE_EXPLOSION_IN_FRAME_PROBABILITY:" + std::to_string(MINE_EXPLOSION_IN_FRAME_PROBABILITY) + ";");
    buffer.append("DUMB_MOVE_PROBABILITY:" + std::to_string(DUMB_MOVE_PROBABILITY) + ";");
    buffer.append("ARCHER_SPAWNING_PROBABILITY:" + std::to_string(ARCHER_SPAWNING_PROBABILITY) + ";");
    buffer.append("ARCHER_MOVING_PROBABILITY:" + std::to_string(ARCHER_MOVING_PROBABILITY) + ";");
    buffer.append("ARCHER_SHOOTING_PROBABILITY:" + std::to_string(ARCHER_SHOOTING_PROBABILITY) + ";");
    buffer.append("WORM_TURNING_PROBABILITY:" + std::to_string(WORM_TURNING_PROBABILITY) + ";");
    buffer.append("WORM_SPAWNING_PROBABILITY:" + std::to_string(WORM_SPAWNING_PROBABILITY) + ";");
    buffer.append("WORM_EATING_ARCHER_PROBABILITY:" + std::to_string(WORM_EATING_ARCHER_PROBABILITY) + ";");
    buffer.append("WORM_EATING_TAIL_PROBABILITY:" + std::to_string(WORM_EATING_TAIL_PROBABILITY) + ";");
    buffer.append("WORM_MOVING_PROBABILITY:" + std::to_string(WORM_MOVING_PROBABILITY) + ";");
    buffer.append("CLAY_RELEASE_PROBABILITY:" + std::to_string(CLAY_RELEASE_PROBABILITY) + ";");
    buffer.append("INITIAL_ARCHERS:" + std::to_string(INITIAL_ARCHERS) + ";");
    buffer.append("INITIAL_WORMS:" + std::to_string(INITIAL_WORMS) + ";");
    buffer.append("WORM_LENGTH:" + std::to_string(WORM_LENGTH) + ";");

    buffer.append(InavjagaGSPIO::constantsTermination);
    /// @warning There may be an off by one as the client expects '\0'
    writeAll(socketfd, buffer.c_str(), buffer.size());
    return this->recvBool(3000);
}

/**
 * @note Probably the most unsafe funciton I have ever written
 * @related https://stackoverflow.com/questions/79938349/achieve-getdelim-functionality-but-on-a-socket-rather-than-on-a-file
 * @related https://stackoverflow.com/a/7781019/15888601
 * @note We could first read everything to a buffer
 * @note Or we could also read character by character and do the finite state machine thingy
 */
std::map<std::string, std::variant<int, float>> InavjagaGSPIO::recvConstants() {
    std::map<std::string, std::variant<int, float>> constants;
    std::variant<int, float> value;
    float floatValue;
    int intValue;

    char buffer[100] = {0};
    char valueBuffer[100] = {0};
    char current = 0;
    size_t insertionIndex = 0;

    while (true) {
        insertionIndex = 0;
        do {
            errno = 0;
            if (int rc = recv(this->socketfd, &current, 1, 0); rc < 0) {
                std::cerr << "Receiving failed with error " << rc << " (" << errno << ")" << std::endl;
                throw std::runtime_error("Error receiving constants from the server");
            }
            #if DEBUG
            std::cerr << current << std::flush;
            #endif
            buffer[insertionIndex++] = current;
        } while(current != ':');
        buffer[insertionIndex] = '\0';
        if (buffer[0] == InavjagaGSPIO::constantsTermination[0]) {
            break;
        }

        insertionIndex = 0;
        do {
            errno = 0;
            if (int rc = recv(this->socketfd, &current, 1, 0); rc < 0) {
                std::cerr << "Receiving failed with error " << rc << " (" << errno << ")" << std::endl;
                throw std::runtime_error("Error receiving constants from the server");
            }
            #if DEBUG
            std::cerr << current << std::flush;
            #endif
            valueBuffer[insertionIndex++] = current;
        } while(current != ';');
        valueBuffer[insertionIndex] = '\0';

        size_t length = strlen(valueBuffer); // Subtract one to remove ';'
        size_t indexRead = 0;
        std::string stringValue(valueBuffer, valueBuffer + length - 1);
        // https://stackoverflow.com/a/45941428/15888601
        intValue = std::stoi(stringValue, &indexRead);
        if (indexRead < length - 1) { // It was likely a float then
            indexRead = 0;
            floatValue = std::stof(stringValue, &indexRead);
            if (indexRead < length - 1) {
                std::cerr << "Received " << stringValue << " was neither integer nor float" << std::endl;
                continue;
            } else {
                value = floatValue;
            }
        } else {
            value = intValue;
        }
        std::string constantName(buffer);
        constants[constantName.substr(0, constantName.size() - 1)] = value;
    }
    #if DEBUG
    for (auto const& [constant, value] : constants) {
        if (std::holds_alternative<int>(value)) {
            std::cerr << "{" << constant << ": Int(" << std::get<int>(value) << ")}" << std::endl;
        } else if (std::holds_alternative<float>(value)) {
            std::cerr << "{" << constant << ": Float(" << std::get<float>(value) << ")}" << std::endl;
        }
    }
    #endif
    return constants;
}

bool ServerInavjagaGSPIO::recvReady(int timeout) {
    char inputBuffer[2] = {0};
    struct pollfd pollFd = {0,0,0};
    pollFd.fd = this->socketfd;
    pollFd.events = POLLIN;
    pollFd.revents = 0;
    errno = 0;
    if (int rc = poll(&pollFd, 1, timeout); rc < 0) {
        std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
        return false;
    }
    if (pollFd.revents & POLLIN) {
        errno = 0;
        if (int rc = recv(socketfd, inputBuffer, (size_t)2, 0); rc < 0) {
            std::cerr << "Receiving failed with error " << rc << " (" << errno << ")" << std::endl;
            return false;
        }
        /// @todo fix, this is dangerous asf
        if (strcmp(InavjagaGSPIO::acceptMessage, inputBuffer) == 0) {
            return true;
        }
    }
    return false;
}

/** @brief Communicates to the server that the client is ready
 * @note The operations up to this point are assumed to be synchronous,
 *       which explains why the mutex is not used for the lock here
 */
void ClientInavjagaGSPIO::sendReady() {
    std::unique_lock lock(outputMutex);
    writeAll(socketfd, acceptMessage, 2);
}

void InavjagaGSPIO::sendCoordinates(const sista::Coordinates& coordinates) const {
    std::string str = "{" + std::to_string(coordinates.y) + "," + std::to_string(coordinates.x) + "}";
    char buffer[10] = {0};
    std::copy(str.c_str(), str.c_str() + str.length(), buffer);
    std::unique_lock lock(outputMutex);
    writeAll(socketfd, buffer, 10);
}

bool InavjagaGSPIO::recvBool(int timeout) const {
    char inputBuffer[2] = {0};
    struct pollfd pollFd = {0,0,0};
    pollFd.fd = this->socketfd;
    pollFd.events = POLLIN;
    pollFd.revents = 0;
    errno = 0;
    if (int rc = poll(&pollFd, 1, timeout); rc < 0) {
        std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
        throw std::runtime_error("Did not receive a boolean answer within the specified timeout");
    }
    if (pollFd.revents & POLLIN) {
        errno = 0;
        if (int rc = recv(socketfd, inputBuffer, (size_t)2, 0); rc < 0) {
            std::cerr << "Receiving failed with error " << rc << " (" << errno << ")" << std::endl;
        }
        #if DEBUG
        std::cerr << "We received " << inputBuffer << std::endl;
        #endif
        /// @todo fix, this is dangerous asf
        if (strcmp(InavjagaGSPIO::yesMessage, inputBuffer) == 0) {
            return true;
        } else if (strcmp(InavjagaGSPIO::noMessage, inputBuffer) == 0) {
            return false;
        }
    }
    throw std::runtime_error("Did not receive a boolean answer within the specified timeout");
}

void InavjagaGSPIO::sendYes() {
    std::unique_lock lock(outputMutex);
    writeAll(socketfd, InavjagaGSPIO::yesMessage, 2);
}

void InavjagaGSPIO::sendNo() {
    std::unique_lock lock(outputMutex);
    writeAll(socketfd, InavjagaGSPIO::noMessage, 2);
}

bool InavjagaGSPIO::waitYes(int timeout) {
    /// @todo this one absolutely needs rewriting with poll()
    char inputBuffer[2] = {0};
    /// @warning we are not locking the mutex, but where should we do that?
    std::future<ssize_t> input_ = std::async(recv, socketfd, inputBuffer, (size_t)2, 0);
    std::future_status status = input_.wait_for(std::chrono::milliseconds(timeout));
    if (status == std::future_status::ready) {
        int rc = input_.get();
        if (rc < 0) return false;
        /// @todo fix, this is dangerous asf
        if (strcmp(InavjagaGSPIO::yesMessage, inputBuffer) == 0) {
            return true;
        }
    }
    return false;
}

void InavjagaGSPIO::sendSyncData(const std::string& message) {
    std::unique_lock lock(syncMutex);
    size_t length = message.length();
    int32_t convertedLength = htonl(length);
    writeAll(this->syncsocketfd, &convertedLength, sizeof(convertedLength));
    writeAll(this->syncsocketfd, message.c_str(), message.length());
}

std::string InavjagaGSPIO::recvSyncData(int timeout) {
    int initialTimeout = timeout;
    auto start = std::chrono::high_resolution_clock::now();
    struct pollfd pollFd_ = {0,0,0};
    pollFd_.fd = this->syncsocketfd;
    pollFd_.events = POLLIN;
    if (int rc = poll(&pollFd_, 1, timeout); rc < 0) {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
        throw std::runtime_error("Polling failed");
    }
    std::string received;
    if (pollFd_.revents & POLLIN) {
        int32_t convertedSize;
        if (int rc = read(syncsocketfd, &convertedSize, sizeof(convertedSize)); rc < 0) {
            std::unique_lock lock(stderrMutex);
            std::cerr << "Receiving message length failed with error " << rc
                      << " (" << errno << ")" << std::endl;
            throw std::runtime_error("Receiving message length failed");
        }
        size_t size = ntohl(convertedSize);
        char* buffer = (char*)calloc(size, sizeof(char));
        do {
            timeout = initialTimeout - std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now() - start
            ).count();
            #if DEBUG
            {
                std::unique_lock lock(stderrMutex);
                std::cerr << "\tTime left: " << timeout << "ms" << std::endl;
            }
            #endif
            pollFd_ = {0,0,0};
            pollFd_.fd = this->syncsocketfd;
            pollFd_.events = POLLIN;
            if (int rc = poll(&pollFd_, 1, timeout); rc < 0) {
                std::unique_lock lock(stderrMutex);
                std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
                throw std::runtime_error("Polling failed");
            }
            if (pollFd_.revents & POLLIN) {
                #if DEBUG
                {
                    std::unique_lock lock(stderrMutex);
                    std::cerr << "Time to read " << size << " characters from the server." << std::endl;
                }
                #endif
                if (int rc = recv(syncsocketfd, buffer, size, MSG_WAITALL); rc < 0) {
                    std::unique_lock lock(stderrMutex);
                    std::cerr << "Receiving message failed with error " << rc
                            << " (" << errno << ")" << std::endl;
                    throw std::runtime_error("Receiving message failed");
                } else if (rc == 0) {
                    std::unique_lock lock(stderrMutex);
                    std::cerr << "Receiving message enountered EOF, since rc=" << rc
                            << " (" << errno << ")" << std::endl;
                    free(buffer);
                    return received;
                } else if (rc > 0) {
                    size -= rc;
                }
                received.append(std::string(buffer));
                if (size == 0) {
                    free(buffer);
                    return received;
                }
            } else {
                #if DEBUG
                std::unique_lock lock(stderrMutex);
                std::cerr << "\tNo message ready yet" << std::endl;
                #endif
                free(buffer);
                return received;
            }
        } while (timeout > 0);
    } else {
        #if DEBUG
        std::unique_lock lock(stderrMutex);
        std::cerr << "No message size ready yet" << std::endl;
        #endif
        return "";
    }
    return received;
}

bool ServerInavjagaGSPIO::offerCoordinates(const sista::Coordinates& coordinates) const {
    this->sendCoordinates(coordinates);
    try {
        return this->recvBool();
    } catch (std::runtime_error& e) {
        std::cerr << e.what();
        return false;
    }
}

sista::Coordinates ClientInavjagaGSPIO::recvCoordinates() const {
    char buffer[10] = {0};
    int rc = recv(socketfd, &buffer, 10, 0);
    if (rc < 0) {
        std::cerr << "Failed to receive coordinates from the server" << std::endl;
        throw std::runtime_error("Failed to receive coordinates from the server");
    }
    sista::Coordinates coordinates;
    sscanf(buffer, "{%hu,%hu}", &coordinates.y, &coordinates.x);
    return coordinates;
}

/** @brief Receives players from a server
 * @warning This operates with the constraint of at most 10 players
 * @note At the moment the parameter current is ignored
 * @param players The players to be transmitted
 * @param current The identifier of the player of the client to whom we are transmitting
 * @return A collection of all the received players
 */
void ServerInavjagaGSPIO::sendPlayers(std::vector<std::shared_ptr<Player>>& players, player_id_t current) {
    char playerIdConverted = '0' + current;
    {
        std::unique_lock lock(outputMutex);
        writeAll(socketfd, &playerIdConverted, 1);
    }
    char identifier = '0';
    for (size_t i = 0; i < players.size(); i++) {
        if (players[i] == nullptr) continue;
        {
            std::unique_lock lock(outputMutex);
            writeAll(socketfd, &identifier, 1);
        }
        this->sendCoordinates(players[i]->getCoordinates());
        identifier++;
    }
    {
        identifier = InavjagaGSPIO::constantsTermination[0];
        std::unique_lock lock(outputMutex);
        writeAll(socketfd, &identifier, 1);
    }
}

/** @brief Receives players from a server
 * @warning This operates with the constraint of at most 10 players
 * @note The returned vector is padded to 10 players with nullptr
 * @return A collection of all the received players
 */
std::vector<std::shared_ptr<Player>> ClientInavjagaGSPIO::recvPlayers() {
    std::vector<std::shared_ptr<Player>> players(10, nullptr);
    sista::Coordinates coordinates;
    char identifier = 0;
    recv(socketfd, &identifier, 1, 0);
    Player::localPlayerId = identifier - '0';
    #if DEBUG
    std::cerr << "Set the localPlayerId to " << Player::localPlayerId << std::endl;
    #endif
    while (true) {
        #if DEBUG
        std::cerr << "Waiting for player identifier..." << std::endl;
        #endif
        recv(socketfd, &identifier, 1, 0);
        #if DEBUG
        std::cerr << "Received:" << identifier << ";" << std::endl;
        #endif
        if (identifier == InavjagaGSPIO::constantsTermination[0]) {
            break;
        }
        coordinates = this->recvCoordinates();
        if (identifier - '0' != Player::localPlayerId) {
            players[identifier - '0'] = std::make_shared<Player>(coordinates);
            players[identifier - '0']->id = identifier - '0';
            players[identifier - '0']->respawnCoordinates = coordinates;
            players[identifier - '0']->mode = Player::Mode::BULLET;
        } else if (identifier - '0' == Player::localPlayerId) {
            Player::localPlayer->setSettings(Player::localPlayerStyle);
            players[identifier - '0'] = Player::localPlayer;
            players[identifier - '0']->id = Player::localPlayerId;
        }
    }
    return players;
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

void ServerRemoteInavjagaIO::sendGameStateToAll(const std::string& gameState) {
    for (std::shared_ptr<InavjagaGSPIO> client_ : neighbors) {
        if (client_ == nullptr) continue;
        std::shared_ptr<ServerInavjagaGSPIO> client = std::static_pointer_cast<ServerInavjagaGSPIO>(client_);
        #if DEBUG
        {
            std::unique_lock lock(stderrMutex);
            std::cerr << "We are about to send the game state to " << client << " client" << std::endl;
        }
        #endif
        client->sendSyncData(gameState);
    }
}

ClientRemoteInavjagaIO::ClientRemoteInavjagaIO() {}
ClientRemoteInavjagaIO::ClientRemoteInavjagaIO(
    std::shared_ptr<ClientInavjagaGSPIO> connectionToServer
): RemoteInavjagaIO({nullptr, connectionToServer}) {}

std::string ClientRemoteInavjagaIO::recvGameState(int timeout) {
    return this->neighbors[1]->recvSyncData(timeout);
}
