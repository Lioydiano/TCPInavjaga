#pragma once

#define VERSION "1.1.0"
#define DATE "2026-04-29"
#define AUTHOR "FLAK-ZOSO"

#ifndef SERVER
    #define SERVER 1
    #define CLIENT 0
#endif

#define DEBUG 1
#define INTRO 0 // Set to 0 to disable the intro screen on startup
#define TUTORIAL 0 // Set to 0 to disable the tutorial on startup

// Field size and shape
#define cWIDTH 70 // The width of the field in cells
#define cHEIGHT 30 // The height of the field in cells
#define cTUNNEL_UNIT 3 // The spacing unit for the tunnels (walls are TUNNEL_UNIT cells wide, the tunnels are twice as wide)
#define cPORTALS_PER_LINE 2 // How many portals are there across each wall

// Game speed
#define cFRAME_DURATION 100 // Expressed in milliseconds
#define cBULLET_SPEED 1 // How many cells a bullet travels in one frame

// Inventory
#define cDROP_INVENTORY_ON_DEATH 1 // Set to 0 to lose the player's inventory on death
#define cINITIAL_CLAY 5 // The initial amount of clay the player has
#define cINITIAL_BULLETS 10 // The initial amount of bullets the player has
#define cINITIAL_MEAT 10 // The initial amount of meat the player has

// Loot
#define cLOOT_ARCHER_CLAY 0 // The amount of clay the player gets from an archer
#define cLOOT_ARCHER_BULLETS 3 // The amount of bullets the player gets from an archer
#define cLOOT_ARCHER_MEAT 1 // The amount of meat the player gets from an archer
#define cLOOT_WORM_HEAD_CLAY 3 // The amount of clay the player gets from a worm head
#define cLOOT_WORM_HEAD_BULLETS 0 // The amount of bullets the player gets from a worm head
#define cLOOT_WORM_HEAD_MEAT 3 // The amount of meat the player gets from a worm head

// Costs
// The cost of building a mine
#define cCOST_OF_MINE_CLAY 1
#define cCOST_OF_MINE_BULLETS 3
#define cCOST_OF_MINE_MEAT 0
#define cMEAT_DURATION_PERIOD 50 // Every how many frames the player eats a unit of meat

// Coordinates
// The coordinates where the player spawns
#define cSPAWN_COORDINATES_Y cTUNNEL_UNIT
#define cSPAWN_COORDINATES_X cWIDTH-cTUNNEL_UNIT
// The coordinates where the player respawns after death
#define cRESPAWN_COORDINATES_Y cTUNNEL_UNIT
#define cRESPAWN_COORDINATES_X cWIDTH-cTUNNEL_UNIT

// Damage
#define cMINE_MINIMUM_DAMAGE 1 // The minimum damage a mine can deal
#define cMINE_MAXIMUM_DAMAGE 3 // The maximum damage a mine can deal
#define cMINE_SENSITIVITY_RADIUS 1 // The radius around the mine that triggers it if a worm is present
#define cMINE_DAMAGE_RADIUS 2 // The radius around the mine that deals damage
#define cINITIAL_WALL_STRENGTH 7 // The initial strength of a wall
#define cWORM_HEALTH_POINTS 3 // The initial health points of a worm

// Probabilistic settings (1 = 100%, 0.5 = 50%, etc.)
#define cWALL_WEARING_PROBABILITY 0.10 // The probability of some walls being worn down (losing one strength point) in any given frame
#define cDAMAGED_WALLS_COUNT 5 // The number of walls getting damaged at once if the wall wearing probability is met
#define cMINE_EXPLOSION_IN_FRAME_PROBABILITY 0.10 // The probability of a mine exploding in any given frame given that it has been triggered
#define cDUMB_MOVE_PROBABILITY 0.25 // The probability of an archer moving in a random direction instead of making a decision based on the environment
#define cARCHER_SPAWNING_PROBABILITY 0.02 // The probability of an archer spawning in a given frame
#define cARCHER_MOVING_PROBABILITY 0.33 // The probability of an archer moving in a given frame
#define cARCHER_SHOOTING_PROBABILITY 0.05 // The probability of an archer shooting in a given frame
#define cWORM_TURNING_PROBABILITY 0.10 // The probability of a worm turning in a given frame
#define cWORM_SPAWNING_PROBABILITY 0.004 // The probability of a worm spawning in a given frame
#define cWORM_EATING_ARCHER_PROBABILITY 0.1 // The probability of a worm eating an archer when meeting it
#define cWORM_EATING_TAIL_PROBABILITY 0.2 // The probability of a worm eating a piece of its own tail when stumbling upon it
#define cWORM_MOVING_PROBABILITY 0.75 // The probability of a worm moving in any given frame
#define cCLAY_RELEASE_PROBABILITY 0.01 // The probability of a worm releasing clay when it moves

// Spawn settings
#define cINITIAL_ARCHERS 6 // The number of archers to spawn at the start of the game
#define cINITIAL_WORMS 1 // The number of worms to spawn at the start of the game

// Worm
#define cWORM_LENGTH 7 // The maximum length to which a worm can grow

inline int WIDTH;
inline int HEIGHT;
inline int TUNNEL_UNIT;
inline int PORTALS_PER_LINE;
inline int FRAME_DURATION;
inline int BULLET_SPEED;
inline int DROP_INVENTORY_ON_DEATH;
inline int INITIAL_CLAY;
inline int INITIAL_BULLETS;
inline int INITIAL_MEAT;
inline int LOOT_ARCHER_CLAY;
inline int LOOT_ARCHER_BULLETS;
inline int LOOT_ARCHER_MEAT;
inline int LOOT_WORM_HEAD_CLAY;
inline int LOOT_WORM_HEAD_BULLETS;
inline int LOOT_WORM_HEAD_MEAT;
inline short COST_OF_MINE_CLAY;
inline short COST_OF_MINE_BULLETS;
inline short COST_OF_MINE_MEAT;
inline int MEAT_DURATION_PERIOD;
inline unsigned short SPAWN_COORDINATES_Y;
inline unsigned short SPAWN_COORDINATES_X;
inline unsigned short RESPAWN_COORDINATES_Y;
inline unsigned short RESPAWN_COORDINATES_X;
inline int MINE_MINIMUM_DAMAGE;
inline int MINE_MAXIMUM_DAMAGE;
inline int MINE_SENSITIVITY_RADIUS;
inline int MINE_DAMAGE_RADIUS;
inline int INITIAL_WALL_STRENGTH;
inline int WORM_HEALTH_POINTS;
inline float WALL_WEARING_PROBABILITY;
inline int DAMAGED_WALLS_COUNT;
inline float MINE_EXPLOSION_IN_FRAME_PROBABILITY;
inline float DUMB_MOVE_PROBABILITY;
inline float ARCHER_SPAWNING_PROBABILITY;
inline float ARCHER_MOVING_PROBABILITY;
inline float ARCHER_SHOOTING_PROBABILITY;
inline float WORM_TURNING_PROBABILITY;
inline float WORM_SPAWNING_PROBABILITY;
inline float WORM_EATING_ARCHER_PROBABILITY;
inline float WORM_EATING_TAIL_PROBABILITY;
inline float WORM_MOVING_PROBABILITY;
inline float CLAY_RELEASE_PROBABILITY;
inline int INITIAL_ARCHERS;
inline int INITIAL_WORMS;
inline int WORM_LENGTH;

// Colors
#define RGB_BLACK sista::RGBColor(0,0,0)
#define RGB_ROCKS_FOREGROUND sista::RGBColor(10,10,10)
#define RGB_ROCKS_BACKGROUND sista::RGBColor(100,100,100)
