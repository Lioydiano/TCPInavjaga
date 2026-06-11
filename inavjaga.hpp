#include <sista/sista.hpp>
#include "src/constants.hpp"
#include "src/direction.hpp"
#include "src/inventory.hpp"
#include "src/entity.hpp"
#include "src/portal.hpp"
#include "src/player.hpp"
#include "src/chest.hpp"
#include "src/bullet.hpp"
#include "src/worm.hpp"
#include "src/mine.hpp"
#include "src/wall.hpp"
#include "src/enemyBullet.hpp"
#include "src/archer.hpp"
#include "src/io.hpp"
#include "src/tcp.hpp"
#include <random>
#include <vector>
#include <set>
#include <map>

extern std::set<char> movementKeys;
extern std::random_device randomDevice;
extern std::minstd_rand rng;
extern std::map<int, std::vector<int>> passages; // {y, {x1, x2, x3...}}
extern std::map<int, std::vector<int>> breaches; // {y, {x1, x2, x3...}}
enum EndReason {STARVED=0, SHOT=1, EATEN=2, STABBED=3, TOUCHDOWN=4, QUIT=5};

void setConstantsToReceivedValues(const std::map<std::string, std::variant<int, float>>&);
void placeClientPlayer(std::shared_ptr<ClientInavjagaGSPIO>);
sista::Coordinates negotiateCoordinates(std::weak_ptr<sista::SwappableField>, std::shared_ptr<ServerInavjagaGSPIO>);
sista::Coordinates negotiateCoordinates(std::weak_ptr<sista::SwappableField>, std::shared_ptr<ClientInavjagaGSPIO>);

void generateTunnels();
void intro();
void tutorial();
void spawnInitialEnemies();
void processMoves();
void processFrame();
void fullProcessFrame(int);
bool revivePlayers();
void processDeath(std::shared_ptr<Player>);
void spawnEnemies();
bool endConditions();
void printSideInstructions(int);
void printKeys();
void reprint();
void updateClients(RemoteInavjagaIO*);
int recvUpdates(RemoteInavjagaIO*);
void restoreGameState(const std::string&);
bool restoreGameState(const std::string&, const std::string&);
bool restoreGameState(
    const std::map<Type, std::string>&,
    const std::map<Type, bool>&
);
template <class T> void deserializeEntities(const std::string&);
template <class T> void removeEntityType(std::vector<std::shared_ptr<T>>);
template<typename IO> void input(IO*);
bool isAct(MoveEvent);
bool act(char);
bool act(MoveEvent);
void printEndInformation(EndReason);
void deallocateAll();