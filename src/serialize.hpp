#include "io.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include <sista/coordinates.hpp>

std::string serializeCoordinates(sista::Coordinates);
std::string serializeCoordinates(unsigned short, unsigned short);

std::string serializeArcher(std::shared_ptr<Archer>);
std::string serializeBullet(std::shared_ptr<Bullet>);

std::string serializeGameState();
void sendGameState(std::shared_ptr<ServerInavjagaGSPIO>);
