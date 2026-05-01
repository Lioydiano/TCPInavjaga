#include "constants.hpp"
#include "direction.hpp"
#include "entity.hpp"
#include "inventory.hpp"
#include <unordered_map>
#pragma once

extern const Inventory INITIAL_INVENTORY;
extern std::unordered_map<Direction, sista::Coordinates> directionMap;
extern std::shared_ptr<sista::SwappableField> field;

typedef unsigned short player_id_t;

class Player : public Entity {
public:
    static sista::ANSISettings playerStyle;
    static player_id_t localPlayerId;
    static std::shared_ptr<Player> localPlayer;
    static std::vector<std::shared_ptr<Player>> players;
    enum Mode {
        COLLECT, BULLET,
        DUMPCHEST, TRAP, MINE
    } mode;
    Inventory inventory;
    player_id_t id = 0;

    Player();
    Player(sista::Coordinates);
    void remove() override;

    void move(Direction);
    void shoot(Direction);

    static void disconnectPlayer(player_id_t);
};