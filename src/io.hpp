#include <sys/types.h>
#include <netinet/ip.h>


struct MoveEvent {
    unsigned short playerId;
    char move;
};

/**
 * Provides an abstraction over the input sources
 */
class InavjagaIO {
    virtual MoveEvent getMove();
};

/**
 * Takes input from stdin and writes output as ACT with InavjagaGSP
 */
class LocalInavjagaIO: public InavjagaIO {

};
NEVERMIND, WE NEED TO DIVIDE INPUT AND OUTPUT
THEY CAN SHARE A MUTEX AND A SOCKET AND THAT IS IT

/**
 * Communicates over InavjagaGSP
 */
class RemoteInavjagaIO: public InavjagaIO {

};

/** InavjagaGSP input/output
 * It is the abstraction layer between the struct MoveEvent to be sent/received and a socket
 * @note the socket is typically TCP, but subclasses can implement it to be protocol agnostic
 */
class InavjagaGSPIO {

};

/**
 * @note receives OWN_ACT, sends ACT
 */
class InavjagaServerGSPIO: public InavjagaGSPIO {

};

/**
 * @note receives ACT, sends OWN_ACT
 */
class InavjagaClientGSPIO: public InavjagaGSPIO {

};
