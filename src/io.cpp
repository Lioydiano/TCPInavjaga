#include "include/cross_platform.hpp"
#include "io.hpp"
#include <sys/types.h>
#include <netinet/ip.h>

#define INAVJAGA_PLAYER_ID_IGNORE 0

MoveEvent LocalInavjagaIO::getMove() {
    #if defined(_WIN32) or defined(__linux__)
        return {INAVJAGA_PLAYER_ID_IGNORE, getch()};
    #elif __APPLE__
        return {INAVJAGA_PLAYER_ID_IGNORE, getchar()};
    #endif
}
