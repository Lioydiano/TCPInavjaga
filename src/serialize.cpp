#include "serialize.hpp"
#include "archer.hpp"
#include "bullet.hpp"
#include "chest.hpp"
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
