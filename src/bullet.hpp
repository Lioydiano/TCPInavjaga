#include "direction.hpp"
#include "entity.hpp"
#include <memory>
#pragma once

class Bullet : public Entity {
public:
    static sista::ANSISettings bulletStyle;
    static std::vector<std::shared_ptr<Bullet>> bullets;
    static std::vector<std::shared_ptr<Bullet>>* entities;
    Direction direction;
    bool collided = false;

    Bullet(sista::Coordinates, Direction);
    void remove() override;

    void move();
};