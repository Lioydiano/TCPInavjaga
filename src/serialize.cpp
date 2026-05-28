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

extern std::mutex streamMutex;

const std::string classTermination = "|";

std::string serializeGameState() {
    std::unique_lock lock(streamMutex);
    std::string serialized;
    serialized.append("Archers:");
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
        serialized.append(serialize(player));
        if (player != Player::players.back())
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

std::string serialize(std::shared_ptr<Bullet> bullet) {
    std::ostringstream os(serialize(bullet->getCoordinates()));
    os << ',' << bullet->collided << ',' << bullet->direction;
    return os.str();
}

std::string serialize(std::shared_ptr<Chest> chest) {
    std::ostringstream os(serialize(chest->getCoordinates()));
    os << ",{" << chest->inventory.clay << "," << chest->inventory.bullets << "," << chest->inventory.meat << "}";
    return os.str();
}

std::string serialize(std::shared_ptr<EnemyBullet> enemyBullet) {
    std::ostringstream os(serialize(enemyBullet->getCoordinates()));
    os << ',' << enemyBullet->collided << ',' << enemyBullet->direction;
    return os.str();
}

std::string serialize(std::shared_ptr<Mine> mine) {
    std::ostringstream os(serialize(mine->getCoordinates()));
    os << ',' << mine->triggered;
    return os.str();
}

std::string serialize(std::shared_ptr<Portal> portal) {
    std::ostringstream os(serialize(portal->getCoordinates()));
    return os.str();
}

std::string serialize(std::shared_ptr<Player> player) {
    std::ostringstream os(serialize(player->getCoordinates()));
    os << ',' << player->id << ',' << player->connected << ',' << player->dead
       << ',' << player->mode << ",{" << player->inventory.clay << ","
       << player->inventory.bullets << "," << player->inventory.meat << "}";
    return os.str();
}

std::string serialize(std::shared_ptr<Wall> wall) {
    std::ostringstream os(serialize(wall->getCoordinates()));
    os << ',' << wall->strength;
    return os.str();
}

std::string serialize(std::shared_ptr<Worm> worm) {
    std::ostringstream os(serialize(worm->getCoordinates()));
    os << ',' << worm->direction << ',' << worm->hp << ',' << worm->collided;
    for (std::shared_ptr<WormBody> wormBody : worm->body) {
        os << ',' << serialize(wormBody->getCoordinates());
    }
    return os.str();
}

std::string serialize(sista::Coordinates coordinates) {
    std::ostringstream os;
    os << '{' << coordinates.y << ',' << coordinates.x << '}';
    return os.str();
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

std::mt19937 deserialize(const std::string& state) {
    std::istringstream in(state);
    std::mt19937 rng;
    in >> rng;
    return rng;
}
