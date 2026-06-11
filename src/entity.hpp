#include <sista/sista.hpp>
#include <algorithm>
#include <memory>
#include <vector>
#pragma once

enum Type {
    PLAYER=0,
    BULLET=1,
    WALL=2, // #
    PORTAL=3, // &
    CHEST=4, // C, can be collected by the player
    TRAP=5, // T, will act when stepped on
    MINE=6, // *, will be triggered when passing by

    // Inspired from https://github.com/Lioydiano/Dune
    WORM_HEAD=7, // H
    WORM_BODY=8, // <^v>

    ARCHER=9, // A
    ENEMY_BULLET=10,
};

class Entity : public sista::Pawn {
public:
    Type type;

    Entity(char, sista::Coordinates, sista::ANSISettings&, Type);
    virtual ~Entity() {}
    virtual void remove() = 0;

protected:
    template <typename T>
    static std::shared_ptr<T> keepAliveFrom(const std::vector<std::shared_ptr<T>>& owners, const T* raw) {
        auto it = std::find_if(owners.begin(), owners.end(), [raw](const std::shared_ptr<T>& p) { return p.get() == raw; });
        if (it == owners.end()) {
            return nullptr;
        }
        return *it;
    }

    template <typename T>
    static void removeOwner(std::vector<std::shared_ptr<T>>& owners, const T* raw) {
        auto it = std::find_if(owners.begin(), owners.end(), [raw](const std::shared_ptr<T>& p) { return p.get() == raw; });
        if (it != owners.end()) {
            owners.erase(it);
        }
    }
};