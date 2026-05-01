#include "include/cross_platform.hpp"
#include "io.hpp"

#define INAVJAGA_PLAYER_ID_IGNORE 0

MoveEvent LocalInavjagaIO::getMove() {
    #if defined(_WIN32) or defined(__linux__)
        return {INAVJAGA_PLAYER_ID_IGNORE, getch()};
    #elif __APPLE__
        return {INAVJAGA_PLAYER_ID_IGNORE, getchar()};
    #endif
}

/** @brief Waits for a move event and returns it
 * @note polls asynchronously all the sockets it owns
 * @note if it is the server, then it will poll multiple ones
 * @return a move event representing the received move
 */
MoveEvent RemoteInavjagaIO::getMove() {

}
