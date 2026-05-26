#include "serialize.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include "chest.hpp"
#include "enemyBullet.hpp"
#include "mine.hpp"
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
    
    /// @todo All the other parts that should be provided
}

std::string serialize(std::shared_ptr<Archer> archer) {
    return serializeCoordinates(archer->getCoordinates());
}

std::string serialize(std::shared_ptr<Bullet> bullet) {
    std::ostringstream os(serializeCoordinates(bullet->getCoordinates()));
    os << ',' << bullet->collided << ',' << bullet->direction;
    return os.str();
}

std::string serialize(std::shared_ptr<Chest> chest) {
    std::ostringstream os(serializeCoordinates(chest->getCoordinates()));
    os << ",{" << chest->inventory.clay << "," << chest->inventory.bullets << "," << chest->inventory.meat << "}";
    return os.str();
}

std::string serialize(std::shared_ptr<EnemyBullet> enemyBullet) {
    std::ostringstream os(serializeCoordinates(enemyBullet->getCoordinates()));
    os << ',' << enemyBullet->collided << ',' << enemyBullet->direction;
    return os.str();
}

std::string serialize(std::shared_ptr<Mine> mine) {
    std::ostringstream os(serializeCoordinates(mine->getCoordinates()));
    os << ',' << mine->triggered;
    return os.str();
}

std::string serialize(std::shared_ptr<Player> player) {
    std::ostringstream os(serializeCoordinates(player->getCoordinates()));
    os << ',' << player->id << ',' << player->connected << ',' << player->dead
       << ',' << player->mode << ",{" << player->inventory.clay << ","
       << player->inventory.bullets << "," << player->inventory.meat << "}";
    return os.str();
}

std::string serialize(sista::Coordinates coordinates) {
    std::ostringstream os;
    os << '{' << coordinates.y << ',' << coordinates.x << '}';
    return os.str();
}

std::string serializeCoordinates(unsigned short y, unsigned short x) {
    std::ostringstream os;
    os << '{' << y << ',' << x << '}';
    return os.str();
}
