#include "io.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include "chest.hpp"
#include "enemyBullet.hpp"
#include "mine.hpp"
#include "player.hpp"
#include <sista/coordinates.hpp>

std::string serializeCoordinates(sista::Coordinates);
std::string serializeCoordinates(unsigned short, unsigned short);

std::string serialize(std::shared_ptr<Archer>);
std::string serialize(std::shared_ptr<Bullet>);
std::string serialize(std::shared_ptr<Chest>);
std::string serialize(std::shared_ptr<EnemyBullet>);
std::string serialize(std::shared_ptr<Mine>);
std::string serialize(std::shared_ptr<Player>);

std::string serializeGameState();
void sendGameState(std::shared_ptr<ServerInavjagaGSPIO>);
