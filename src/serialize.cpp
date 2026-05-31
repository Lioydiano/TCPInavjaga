#include "serialize.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include "chest.hpp"
#include "enemyBullet.hpp"
#include "mine.hpp"
#include "portal.hpp"
#include "wall.hpp"
#include "worm.hpp"
#include <sstream>
#include <memory>
#if DEBUG
#include <iostream>
#endif

extern std::mutex streamMutex;
extern std::mutex stderrMutex;

const std::string classTermination = "|";

std::string serializeGameState() {
    std::unique_lock lock(streamMutex);
    std::string serialized;
    serialized.append(classTermination);
    for (std::shared_ptr<Archer> archer : Archer::archers) {
        serialized.append(serialize(archer));
        if (archer != Archer::archers.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<Bullet> bullet : Bullet::bullets) {
        serialized.append(serialize(bullet));
        if (bullet != Bullet::bullets.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<Chest> chest : Chest::chests) {
        serialized.append(serialize(chest));
        if (chest != Chest::chests.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<EnemyBullet> enemyBullet : EnemyBullet::enemyBullets) {
        serialized.append(serialize(enemyBullet));
        if (enemyBullet != EnemyBullet::enemyBullets.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<Mine> mine : Mine::mines) {
        serialized.append(serialize(mine));
        if (mine != Mine::mines.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<Player> player : Player::players) {
        if (player == nullptr) continue;
        serialized.append(serialize(player));
        // if (player != Player::players.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    /// @warning This works under the assumption that portals are sorted
    /// in such a way that two portals pointing at each other are subsequent
    for (std::shared_ptr<Portal> portal : Portal::portals) {
        serialized.append(serialize(portal));
        if (portal != Portal::portals.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<Wall> wall : Wall::walls) {
        serialized.append(serialize(wall));
        if (wall != Wall::walls.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    for (std::shared_ptr<Worm> worm : Worm::worms) {
        serialized.append(serialize(worm));
        if (worm != Worm::worms.back())
            serialized.append(";");
    }
    serialized.append(classTermination);
    return serialized;
}

std::string serialize(std::shared_ptr<Archer> archer) {
    return serialize(archer->getCoordinates());
}
// https://stackoverflow.com/a/4933205/15888601
template <> std::shared_ptr<Archer> deserialize(const std::string& entity) {
    return std::make_shared<Archer>(deserializeCoordinates(entity));
}

std::string serialize(std::shared_ptr<Bullet> bullet) {
    std::ostringstream os;
    os << serialize(bullet->getCoordinates()) << ':' << bullet->collided << ':' << bullet->direction;
    return os.str();
}
template <> std::shared_ptr<Bullet> deserialize(const std::string& entity) {
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Trying to deserialize Bullet: " << entity << std::endl;
    }
    #endif
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::string direction;
    std::getline(is, direction, ':');
    std::shared_ptr<Bullet> bullet = std::make_shared<Bullet>(
        deserializeCoordinates(coordinates),
        (Direction)std::stoi(direction)
    );
    is >> bullet->collided;
    return bullet;
}

std::string serialize(std::shared_ptr<Chest> chest) {
    std::ostringstream os;
    os << serialize(chest->getCoordinates()) << ":" << serialize(chest->inventory);
    return os.str();
}
template <> std::shared_ptr<Chest> deserialize(const std::string& entity) {
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::string inventory;
    std::getline(is, inventory, ':');
    return std::make_shared<Chest>(
        deserializeCoordinates(coordinates),
        deserializeInventory(inventory)
    );
}

std::string serialize(std::shared_ptr<EnemyBullet> enemyBullet) {
    std::ostringstream os;
    os << serialize(enemyBullet->getCoordinates()) << ':' << enemyBullet->collided << ':' << enemyBullet->direction;
    return os.str();
}
template <> std::shared_ptr<EnemyBullet> deserialize(const std::string& entity) {
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Trying to deserialize EnemyBullet: " << entity << std::endl;
    }
    #endif
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::string direction;
    std::getline(is, direction, ':');
    std::shared_ptr<EnemyBullet> bullet = std::make_shared<EnemyBullet>(
        deserializeCoordinates(coordinates),
        (Direction)std::stoi(direction)
    );
    is >> bullet->collided;
    return bullet;
}

std::string serialize(std::shared_ptr<Mine> mine) {
    std::ostringstream os;
    os << serialize(mine->getCoordinates()) << ':' << mine->triggered;
    return os.str();
}
template <> std::shared_ptr<Mine> deserialize(const std::string& entity) {
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::shared_ptr<Mine> mine = std::make_shared<Mine>(
        deserializeCoordinates(coordinates)
    );
    is >> mine->triggered;
    return mine;
}

std::string serialize(std::shared_ptr<Portal> portal) {
    return serialize(portal->getCoordinates());
}
template <> std::shared_ptr<Portal> deserialize(const std::string& entity) {
    return std::make_shared<Portal>(deserializeCoordinates(entity));
}

std::string serialize(std::shared_ptr<Player> player) {
    std::ostringstream os;
    os << serialize(player->getCoordinates()) << ':' << player->id << ':' << player->connected << ':' << player->dead
       << ':' << player->mode << ":" << serialize(player->inventory);
    return os.str();
}
template <> std::shared_ptr<Player> deserialize(const std::string& entity) {
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::shared_ptr<Player> player = std::make_shared<Player>(
        deserializeCoordinates(coordinates)
    );
    char separator;
    is >> separator >> player->id >> separator >> player->connected >> separator >> player->dead >> separator;
    int mode;
    is >> mode >> separator;
    player->mode = (Player::Mode)mode;
    return player;
}

std::string serialize(std::shared_ptr<Wall> wall) {
    std::ostringstream os;
    os << serialize(wall->getCoordinates()) << ':' << wall->strength;
    return os.str();
}
template <> std::shared_ptr<Wall> deserialize(const std::string& entity) {
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    short int strength;
    is >> strength;
    std::shared_ptr<Wall> wall = std::make_shared<Wall>(
        deserializeCoordinates(coordinates), strength
    );
    return wall;
}

std::string serialize(std::shared_ptr<Worm> worm) {
    std::ostringstream os;
    os << serialize(worm->getCoordinates()) << ':' << worm->direction << ':' << worm->hp << ':' << worm->collided;
    for (std::shared_ptr<WormBody> wormBody : worm->body) {
        os << ':' << serialize(wormBody->getCoordinates()) << ':' << wormBody->direction;
    }
    return os.str();
}
template <> std::shared_ptr<Worm> deserialize(const std::string& entity) {
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Trying to deserialize Worm: " << entity << std::endl;
    }
    #endif
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::string direction;
    std::getline(is, direction, ':');
    std::shared_ptr<Worm> worm = std::make_shared<Worm>(
        deserializeCoordinates(coordinates)
    );
    char separator;
    is >> separator >> worm->hp >> separator >> worm->collided;
    std::string wormBodyString;
    while (std::getline(is, wormBodyString, ':')) {
        std::istringstream wormBodyIS(wormBodyString.substr(1));
        std::getline(wormBodyIS, coordinates, ':');
        wormBodyIS >> separator;
        std::string direction;
        std::getline(wormBodyIS, direction, ':');
        worm->body.push_back(std::make_shared<WormBody>(
            deserializeCoordinates(wormBodyString), (Direction)std::stoi(direction)
        ));
    }
    return worm;
}

std::string serialize(const Inventory& inventory) {
    return "{" + std::to_string(inventory.clay)
        + "," + std::to_string(inventory.bullets)
        + "," + std::to_string(inventory.meat) + "}";
}

Inventory deserializeInventory(const std::string& inventory) {
    Inventory inv;
    std::istringstream is(inventory);
    is.ignore(1); // Should ignore a '{', but is it worth failing silently?
    is >> inv.clay;
    is.ignore(1); // Should ignore a ',', but is it worth failing silently?
    is >> inv.bullets;
    is.ignore(1); // Should ignore a ',', but is it worth failing silently?
    is >> inv.meat;
    is.ignore(1); // Should ignore a '}', but is it worth failing silently?
    return inv;
}

std::string serialize(sista::Coordinates coordinates) {
    std::ostringstream os;
    os << '{' << coordinates.y << ',' << coordinates.x << '}';
    return os.str();
}

sista::Coordinates deserializeCoordinates(const std::string& coordinates) {
    sista::Coordinates coords;
    std::istringstream is(coordinates);
    is.ignore(1); // Should ignore a '{', but is it worth failing silently?
    is >> coords.y;
    is.ignore(1); // Should ignore a ',', but is it worth failing silently?
    is >> coords.x;
    is.ignore(1); // Should ignore a '}', but is it worth failing silently?
    return coords;
}

std::string serialize(unsigned short y, unsigned short x) {
    std::ostringstream os;
    os << '{' << y << ',' << x << '}';
    return os.str();
}

/** @brief Serializes a Mersenne Twister random engine
 * @cite https://en.cppreference.com/cpp/numeric/random/mersenne_twister_engine/operator_ltltgtgt
*/
std::string serialize(const std::mt19937& rng) {
    std::ostringstream out;
    out << rng;
    return out.str();
}

std::mt19937 deserializeRng(const std::string& state) {
    std::istringstream in(state);
    std::mt19937 rng;
    in >> rng;
    return rng;
}
