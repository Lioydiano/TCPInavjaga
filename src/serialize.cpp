#include "serialize.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include "chest.hpp"
#include "enemyBullet.hpp"
#include "mine.hpp"
#include "portal.hpp"
#include "wall.hpp"
#include "worm.hpp"
#include "entity.hpp"
#include <sstream>
#include <memory>
#if DEBUG
#include <iostream>
extern std::minstd_rand rng;
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

/** @brief Splits the game state into its sections
 * @param gameState The game state without the frame number and the PRNG,
 *                  starting from the first archer
 * @note The key `Type::WORM_HEAD` represents Worms,
 *       that are serialized together with WormBody,
 *       thus there is no key `Type::WORM_BODY`
 * @return The mapping between category of entities
 *         and their serialized representation
 */
std::map<Type, std::string> splitGameState(std::istringstream& gameState) {
    std::map<Type, std::string> entitySections;
    std::getline(gameState, entitySections[Type::ARCHER], classTermination[0]);
    std::getline(gameState, entitySections[Type::BULLET], classTermination[0]);
    std::getline(gameState, entitySections[Type::CHEST], classTermination[0]);
    std::getline(gameState, entitySections[Type::ENEMY_BULLET], classTermination[0]);
    std::getline(gameState, entitySections[Type::MINE], classTermination[0]);
    std::getline(gameState, entitySections[Type::PLAYER], classTermination[0]);
    std::getline(gameState, entitySections[Type::PORTAL], classTermination[0]);
    std::getline(gameState, entitySections[Type::WALL], classTermination[0]);
    std::getline(gameState, entitySections[Type::WORM_HEAD], classTermination[0]);
    return entitySections;
}
/**
 * @note This overload expects a full game state, including the prefix to discard
 */
std::map<Type, std::string> splitGameState(const std::string& gameState) {
    std::istringstream is(gameState);
    std::string _;
    std::getline(is, _, ','); // Toss the frame counter
    std::getline(is, _, ','); // Toss the random state
    char _classTermination;
    is >> _classTermination; // Ignore the classTermination[0]
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        std::cerr << "Passing to splitGameState: " << is.str().substr( is.tellg() ) << std::endl;
    }
    #endif
    return splitGameState(is);
}

/**
 * @return Map where each changed entity type is associated to whether it mismatches
 */
std::map<Type, bool> compareGameStates(
    const std::map<Type, std::string>& current,
    const std::map<Type, std::string>& incoming
) {
    std::map<Type, bool> comparison;
    comparison[Type::ARCHER] = current.at(Type::ARCHER) != incoming.at(Type::ARCHER);
    comparison[Type::BULLET] = current.at(Type::BULLET) != incoming.at(Type::BULLET);
    comparison[Type::CHEST] = current.at(Type::CHEST) != incoming.at(Type::CHEST);
    comparison[Type::ENEMY_BULLET] = current.at(Type::ENEMY_BULLET) != incoming.at(Type::ENEMY_BULLET);
    comparison[Type::MINE] = current.at(Type::MINE) != incoming.at(Type::MINE);
    comparison[Type::PLAYER] = current.at(Type::PLAYER) != incoming.at(Type::PLAYER);
    comparison[Type::PORTAL] = current.at(Type::PORTAL) != incoming.at(Type::PORTAL);
    comparison[Type::WALL] = current.at(Type::WALL) != incoming.at(Type::WALL);
    comparison[Type::WORM_HEAD] = current.at(Type::WORM_HEAD) != incoming.at(Type::WORM_HEAD);
    #if DEBUG
    {
        std::unique_lock lock(stderrMutex);
        for (auto const& [type, value] : comparison) {
            std::cerr << "\t\tIncoming: " << incoming.at(type) << "\n";
            std::cerr << "\t\tCurrent: " << current.at(type) << "\n";
            std::cerr << "\t" << type << ": " << value << "\n";
        }
        std::flush(std::cerr);
    }
    #endif
    return comparison;
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
    os << serialize(bullet->getCoordinates()) << ':' << bullet->direction << ':' << bullet->collided;
    return os.str();
}
template <> std::shared_ptr<Bullet> deserialize(const std::string& entity) {
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
    os << serialize(enemyBullet->getCoordinates()) << ':' << enemyBullet->direction << ':' << enemyBullet->collided;
    return os.str();
}
template <> std::shared_ptr<EnemyBullet> deserialize(const std::string& entity) {
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
    os << serialize(player->getCoordinates()) << ':' << serialize(player->respawnCoordinates) << ':'
       << player->id << ':' << player->connected << ':' << player->dead
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
    std::getline(is, coordinates, ':');
    player->respawnCoordinates = deserializeCoordinates(coordinates);
    char separator;
    is >> player->id >> separator >> player->connected >> separator >> player->dead >> separator;
    int mode;
    is >> mode >> separator;
    player->mode = (Player::Mode)mode;
    std::string inventory;
    std::getline(is, inventory);
    player->inventory = deserializeInventory(inventory);
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
    std::istringstream is(entity);
    std::string coordinates;
    std::getline(is, coordinates, ':');
    std::string direction;
    std::getline(is, direction, ':');
    std::shared_ptr<Worm> worm = std::make_shared<Worm>(
        deserializeCoordinates(coordinates),
        (Direction)std::stoi(direction)
    );
    char separator;
    is >> worm->hp >> separator >> worm->collided >> separator;
    std::string wormBodyCoordinates, wormBodyDirection;
    while (std::getline(is, wormBodyCoordinates, ':')) {
        std::getline(is, wormBodyDirection, ':');
        worm->body.push_back(std::make_shared<WormBody>(
            deserializeCoordinates(wormBodyCoordinates),
            (Direction)std::stoi(wormBodyDirection)
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
std::string serialize(const std::minstd_rand& rng) {
    std::ostringstream out;
    out << rng;
    return out.str();
}

std::minstd_rand deserializeRng(const std::string& state) {
    std::istringstream in(state);
    std::minstd_rand rng;
    in >> rng;
    return rng;
}
