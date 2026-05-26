#include "io.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include "chest.hpp"
#include "enemyBullet.hpp"
#include "mine.hpp"
#include "player.hpp"
#include "portal.hpp"
#include "wall.hpp"
#include "worm.hpp"
#include <sista/coordinates.hpp>

std::string serializeCoordinates(sista::Coordinates);
std::string serializeCoordinates(unsigned short, unsigned short);

std::string serialize(std::shared_ptr<Archer>);
std::string serialize(std::shared_ptr<Bullet>);
std::string serialize(std::shared_ptr<Chest>);
std::string serialize(std::shared_ptr<EnemyBullet>);
std::string serialize(std::shared_ptr<Mine>);
std::string serialize(std::shared_ptr<Player>);
std::string serialize(std::shared_ptr<Portal>);
std::string serialize(std::shared_ptr<Wall>);
std::string serialize(std::shared_ptr<Worm>);

std::string serializeGameState();
void sendGameState(std::shared_ptr<ServerInavjagaGSPIO>);
