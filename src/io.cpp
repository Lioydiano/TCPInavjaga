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

uint32_t InavjagaGSPIO::recvRandomSeed(int timeout) {
    // https://stackoverflow.com/a/64357776/15888601
    uint32_t seed;
    #if DEBUG
    std::cerr << "Reading random seed" << std::endl;
    #endif
    struct pollfd pollFds_[1] = {0};
    pollFds_[0].fd = this->socketfd;
    pollFds_[0].events = POLLIN;
    if (int rc = poll(pollFds_, 1, timeout) < 0) {
        std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
        return -1;
    }
    if (pollFds_[0].revents & POLLIN) {
        read(socketfd, &seed, sizeof(uint32_t));
        return ntohl(seed);
    }
    std::cerr << "No message ready yet" << std::endl;
    return -1;
}

void InavjagaGSPIO::sendRandomSeed(uint32_t seed) {
    // https://stackoverflow.com/a/64357776/15888601
    uint32_t converted = htonl(seed);
    std::cerr << converted << " is our integer and its size is " << sizeof(uint32_t) << std::endl;
    std::unique_lock lock(outputMutex);
    if (ssize_t rc = write(this->socketfd, &converted, sizeof(uint32_t)) < 0) {
        std::cerr << "Failed to send random seed with error " << rc << "(" << errno << ")" << std::endl;
    }
}

std::shared_mutex InavjagaGSPIO::outputMutex = std::shared_mutex();

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
    for (std::shared_ptr<InavjagaGSPIO> gspio : this->neighbors) {
        if (!gspio) continue;
        gspio->sendMove(moveEvent);
    }
}

ClientInavjagaGSPIO::ClientInavjagaGSPIO(int sockfd) {
    this->socketfd = sockfd;
}
TCPClientInavjagaGSPIO::TCPClientInavjagaGSPIO(int sockfd): ClientInavjagaGSPIO(sockfd) {}

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
    std::string buffer = "WIDTH:" + std::to_string(WIDTH) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "HEIGHT:" + std::to_string(HEIGHT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "TUNNEL_UNIT:" + std::to_string(TUNNEL_UNIT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "PORTALS_PER_LINE:" + std::to_string(PORTALS_PER_LINE) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "FRAME_DURATION:" + std::to_string(FRAME_DURATION) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "BULLET_SPEED:" + std::to_string(BULLET_SPEED) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "DROP_INVENTORY_ON_DEATH:" + std::to_string(DROP_INVENTORY_ON_DEATH) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "INITIAL_CLAY:" + std::to_string(INITIAL_CLAY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "INITIAL_BULLETS:" + std::to_string(INITIAL_BULLETS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "INITIAL_MEAT:" + std::to_string(INITIAL_MEAT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "LOOT_ARCHER_CLAY:" + std::to_string(LOOT_ARCHER_CLAY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "LOOT_ARCHER_BULLETS:" + std::to_string(LOOT_ARCHER_BULLETS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "LOOT_ARCHER_MEAT:" + std::to_string(LOOT_ARCHER_MEAT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "LOOT_WORM_HEAD_CLAY:" + std::to_string(LOOT_WORM_HEAD_CLAY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "LOOT_WORM_HEAD_BULLETS:" + std::to_string(LOOT_WORM_HEAD_BULLETS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "LOOT_WORM_HEAD_MEAT:" + std::to_string(LOOT_WORM_HEAD_MEAT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "COST_OF_MINE_CLAY:" + std::to_string(COST_OF_MINE_CLAY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "COST_OF_MINE_BULLETS:" + std::to_string(COST_OF_MINE_BULLETS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "COST_OF_MINE_MEAT:" + std::to_string(COST_OF_MINE_MEAT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "MEAT_DURATION_PERIOD:" + std::to_string(MEAT_DURATION_PERIOD) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "SPAWN_COORDINATES_Y:" + std::to_string(SPAWN_COORDINATES_Y) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "SPAWN_COORDINATES_X:" + std::to_string(SPAWN_COORDINATES_X) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "RESPAWN_COORDINATES_Y:" + std::to_string(RESPAWN_COORDINATES_Y) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "RESPAWN_COORDINATES_X:" + std::to_string(RESPAWN_COORDINATES_X) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "MINE_MINIMUM_DAMAGE:" + std::to_string(MINE_MINIMUM_DAMAGE) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "MINE_MAXIMUM_DAMAGE:" + std::to_string(MINE_MAXIMUM_DAMAGE) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "MINE_SENSITIVITY_RADIUS:" + std::to_string(MINE_SENSITIVITY_RADIUS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "MINE_DAMAGE_RADIUS:" + std::to_string(MINE_DAMAGE_RADIUS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "INITIAL_WALL_STRENGTH:" + std::to_string(INITIAL_WALL_STRENGTH) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_HEALTH_POINTS:" + std::to_string(WORM_HEALTH_POINTS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WALL_WEARING_PROBABILITY:" + std::to_string(WALL_WEARING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "DAMAGED_WALLS_COUNT:" + std::to_string(DAMAGED_WALLS_COUNT) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "MINE_EXPLOSION_IN_FRAME_PROBABILITY:" + std::to_string(MINE_EXPLOSION_IN_FRAME_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "DUMB_MOVE_PROBABILITY:" + std::to_string(DUMB_MOVE_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "ARCHER_SPAWNING_PROBABILITY:" + std::to_string(ARCHER_SPAWNING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "ARCHER_MOVING_PROBABILITY:" + std::to_string(ARCHER_MOVING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "ARCHER_SHOOTING_PROBABILITY:" + std::to_string(ARCHER_SHOOTING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_TURNING_PROBABILITY:" + std::to_string(WORM_TURNING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_SPAWNING_PROBABILITY:" + std::to_string(WORM_SPAWNING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_EATING_ARCHER_PROBABILITY:" + std::to_string(WORM_EATING_ARCHER_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_EATING_TAIL_PROBABILITY:" + std::to_string(WORM_EATING_TAIL_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_MOVING_PROBABILITY:" + std::to_string(WORM_MOVING_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "CLAY_RELEASE_PROBABILITY:" + std::to_string(CLAY_RELEASE_PROBABILITY) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "INITIAL_ARCHERS:" + std::to_string(INITIAL_ARCHERS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "INITIAL_WORMS:" + std::to_string(INITIAL_WORMS) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);
    buffer = "WORM_LENGTH:" + std::to_string(WORM_LENGTH) + ";";
    send(socketfd, buffer.c_str(), buffer.length(), 0);

    send(socketfd, InavjagaGSPIO::constantsTermination, sizeof(InavjagaGSPIO::constantsTermination), 0);
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
            if (int rc = recv(this->socketfd, &current, 1, 0) < 0) {
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
            recv(this->socketfd, &current, 1, 0); // We throw away a '\0' terminator
            break;
        }

        insertionIndex = 0;
        do {
            errno = 0;
            if (int rc = recv(this->socketfd, &current, 1, 0) < 0) {
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

/** @brief Communicates to the server that the client is ready
 * @note The operations up to this point are assumed to be synchronous,
 *       which explains why the mutex is not used for the lock here
 */
void ClientInavjagaGSPIO::sendReady() {
    send(socketfd, acceptMessage, 2, 0);
}

void InavjagaGSPIO::sendCoordinates(const sista::Coordinates& coordinates) const {
    std::string str = "{" + std::to_string(coordinates.y) + "," + std::to_string(coordinates.x) + "}";
    char buffer[10] = {0};
    std::copy(str.c_str(), str.c_str() + str.length(), buffer);
    std::unique_lock lock(outputMutex);
    send(socketfd, buffer, 10, 0);
}

bool InavjagaGSPIO::recvBool(int timeout) const {
    char inputBuffer[2] = {0};
    struct pollfd pollFd = {0};
    pollFd.fd = this->socketfd;
    pollFd.events = POLLIN;
    pollFd.revents = 0;
    errno = 0;
    if (int rc = poll(&pollFd, 1, timeout) < 0) {
        std::cerr << "Polling failed with error " << rc << " (" << errno << ")" << std::endl;
        throw std::runtime_error("Did not receive a boolean answer within the specified timeout");
    }
    if (pollFd.revents & POLLIN) {
        errno = 0;
        if (int rc = recv(socketfd, inputBuffer, (size_t)2, 0) < 0) {
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
    send(socketfd, InavjagaGSPIO::yesMessage, 2, 0);
}

void InavjagaGSPIO::sendNo() {
    send(socketfd, InavjagaGSPIO::noMessage, 2, 0);
}

bool InavjagaGSPIO::waitYes(int timeout) {
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

bool ServerInavjagaGSPIO::offerCoordinates(const sista::Coordinates& coordinates) const {
    this->sendCoordinates(coordinates);
    try {
        return this->recvBool();
    } catch (std::runtime_error& e) {
        std::cerr << e.what();
        return false;
    }
}

sista::Coordinates ClientInavjagaGSPIO::recvCoordinates(int timeout) const {
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
    char identifier = '0';
    for (size_t i = 0; i < players.size(); i++) {
        identifier++;
        if (players[i] == nullptr) continue;
        {
            std::unique_lock lock(outputMutex);
            send(socketfd, &identifier, 1, 0);
        }
        this->sendCoordinates(players[i]->getCoordinates());
    }
    {
        identifier = InavjagaGSPIO::constantsTermination[0];
        std::unique_lock lock(outputMutex);
        send(socketfd, &identifier, 1, 0);
    }
}

/** @brief Receives players from a server
 * @warning This operates with the constraint of at most 10 players
 * @return A collection of all the received players
 */
std::vector<std::shared_ptr<Player>> ClientInavjagaGSPIO::recvPlayers() {
    // ID{y,x};ID{y,x}; [...] ;ID:{y,x};-
    std::vector<std::shared_ptr<Player>> players(10, nullptr);
    sista::Coordinates coordinates;
    char identifier = 0;
    while (true) {
        #if DEBUG
        std::cerr << "Waiting for player identifier..." << std::endl;
        #endif
        recv(socketfd, &identifier, 1, 0);
        if (identifier == InavjagaGSPIO::constantsTermination[0]) {
            break;
        }
        coordinates = this->recvCoordinates();
        if (identifier - '0' == Player::localPlayerId) {
            players[identifier - '0'] = Player::localPlayer;
            continue;
        }
        players[identifier - '0'] = std::make_shared<Player>(coordinates);
        recv(socketfd, &identifier, 1, 0); // We could assert that it is a ';'
    }
    size_t playersCount = 0;
    for (size_t i = players.size(); i >= 0; i--) {
        if (players[i] != nullptr) {
            playersCount = i;
        }
    }
    return std::vector<std::shared_ptr<Player>>(players.begin(), players.begin() + playersCount);
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
