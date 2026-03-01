#pragma once
#include <nds.h>

// ---------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------
enum { MAP_TILES = 32, TILE_PX = 16, MAP_PX = MAP_TILES * TILE_PX }; // 512x512
enum { SCREEN_W = 256, SCREEN_H = 192 };

// ---------------------------------------------------------------------------
// Pool sizes
// ---------------------------------------------------------------------------
enum { MAX_UNITS = 50, MAX_BUILDINGS = 30 };
enum { MAX_UNITS_PER_PLAYER = 25, MAX_BUILDINGS_PER_PLAYER = 15 };
enum { NUM_PLAYERS = 2 };

// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------
enum Resource { RES_FOOD = 0, RES_WOOD = 1, RES_GOLD = 2, RES_STONE = 3, RES_COUNT = 4 };

// ---------------------------------------------------------------------------
// Ages
// ---------------------------------------------------------------------------
enum Age { AGE_DARK = 0, AGE_FEUDAL = 1, AGE_CASTLE = 2, AGE_IMPERIAL = 3, AGE_COUNT = 4 };

// ---------------------------------------------------------------------------
// Terrain
// ---------------------------------------------------------------------------
enum TerrainType : u8 {
    TERRAIN_GRASS  = 0,
    TERRAIN_DIRT   = 1,
    TERRAIN_WATER  = 2,
    TERRAIN_FOREST = 3,
    TERRAIN_GOLD   = 4,
    TERRAIN_STONE  = 5,
    TERRAIN_FARM   = 6,
    TERRAIN_COUNT  = 7
};

// ---------------------------------------------------------------------------
// Unit types
// ---------------------------------------------------------------------------
enum UnitTypeId {
    UNIT_VILLAGER  = 0,
    UNIT_MILITIA   = 1,
    UNIT_ARCHER    = 2,
    UNIT_KNIGHT    = 3,
    UNIT_SPEARMAN  = 4,
    UNIT_TYPE_COUNT = 5
};

// ---------------------------------------------------------------------------
// Building types
// ---------------------------------------------------------------------------
enum BuildingTypeId {
    BLDG_TOWN_CENTER   = 0,
    BLDG_HOUSE         = 1,
    BLDG_BARRACKS      = 2,
    BLDG_ARCHERY_RANGE = 3,
    BLDG_STABLE        = 4,
    BLDG_FARM          = 5,
    BLDG_MINING_CAMP   = 6,
    BLDG_LUMBER_CAMP   = 7,
    BLDG_TYPE_COUNT    = 8
};

// ---------------------------------------------------------------------------
// Unit states
// ---------------------------------------------------------------------------
enum UnitState {
    USTATE_IDLE       = 0,
    USTATE_MOVING     = 1,
    USTATE_GATHERING  = 2,
    USTATE_RETURNING  = 3,
    USTATE_ATTACKING  = 4,
    USTATE_DEAD       = 5
};

// ---------------------------------------------------------------------------
// Directions
// ---------------------------------------------------------------------------
enum Direction { DIR_UP = 0, DIR_RIGHT = 1, DIR_DOWN = 2, DIR_LEFT = 3, DIR_COUNT = 4 };

// ---------------------------------------------------------------------------
// Unit stat table
// ---------------------------------------------------------------------------
struct UnitStats {
    s16 hp;
    s16 attack;
    s16 armor;
    s16 range;      // in tiles (1 = melee)
    s16 speed;      // pixels per update tick
    s16 los;        // line of sight in tiles
    s16 trainTime;  // in game frames (60fps)
    int cost[RES_COUNT]; // F, W, G, S
    u8 ageReq;
    u8 bldgReq;     // BuildingTypeId required to train
};

// ---------------------------------------------------------------------------
// Building stat table
// ---------------------------------------------------------------------------
struct BuildingStats {
    s16 hp;
    s16 buildTime;  // frames
    int cost[RES_COUNT]; // F, W, G, S
    u8 ageReq;
    u8 popSpace;    // population provided (houses/TC)
    u8 tileW, tileH;
};

// ---------------------------------------------------------------------------
// Data tables — unit stats
// ---------------------------------------------------------------------------
// Stats based on real AoE2 values (wiki-verified), adapted for DSi:
// - Speed: 1 px/tick ≈ 0.8-1.0 game speed, 2 px/tick ≈ 1.35 (cavalry)
// - Range: tiles (1 = melee, 4 = archer)
// - Train time: scaled to 60fps (real seconds × 60)
// - Armor: combined melee+pierce for simplicity
//                                hp  atk arm rng spd los  train   F    W    G    S   age  bldg
static const UnitStats UNIT_STATS[UNIT_TYPE_COUNT] = {
    /* VILLAGER  */ {  25,  3,  0,  1,  1,  4, 1500, { 50,  0,  0,  0}, AGE_DARK,   BLDG_TOWN_CENTER },
    /* MILITIA   */ {  40,  4,  1,  1,  1,  4, 1260, { 60,  0, 20,  0}, AGE_DARK,   BLDG_BARRACKS },
    /* ARCHER    */ {  30,  4,  0,  4,  1,  6, 2100, {  0, 25, 45,  0}, AGE_FEUDAL, BLDG_ARCHERY_RANGE },
    /* KNIGHT    */ { 100, 10,  2,  1,  2,  4, 1800, { 60,  0, 75,  0}, AGE_CASTLE, BLDG_STABLE },
    /* SPEARMAN  */ {  45,  3,  0,  1,  1,  4, 1320, { 35, 25,  0,  0}, AGE_FEUDAL, BLDG_BARRACKS },
};

// ---------------------------------------------------------------------------
// Data tables — building stats
// ---------------------------------------------------------------------------
// Building stats based on real AoE2 values (wiki-verified), scaled for DSi:
// - HP scaled down (real TC = 2400, too high for DSi combat pace)
// - Build times in frames at 60fps
//                                      hp  build   F    W    G    S   age  pop  tw th
static const BuildingStats BLDG_STATS[BLDG_TYPE_COUNT] = {
    /* TOWN_CENTER   */ { 600, 600, {  0, 275,  0, 100}, AGE_DARK,    5,  2, 2 },
    /* HOUSE         */ { 150, 150, {  0,  25,  0,   0}, AGE_DARK,    5,  1, 1 },
    /* BARRACKS      */ { 350, 300, {  0, 175,  0,   0}, AGE_DARK,    0,  2, 2 },
    /* ARCHERY_RANGE */ { 350, 300, {  0, 175,  0,   0}, AGE_FEUDAL,  0,  2, 2 },
    /* STABLE        */ { 350, 300, {  0, 175,  0,   0}, AGE_CASTLE,  0,  2, 2 },
    /* FARM          */ {  50, 100, {  0,  60,  0,   0}, AGE_DARK,    0,  1, 1 },
    /* MINING_CAMP   */ { 100, 150, {  0, 100,  0,   0}, AGE_DARK,    0,  1, 1 },
    /* LUMBER_CAMP   */ { 100, 150, {  0, 100,  0,   0}, AGE_DARK,    0,  1, 1 },
};

// ---------------------------------------------------------------------------
// Age advancement costs and times
// ---------------------------------------------------------------------------
static const int AGE_COST[AGE_COUNT][RES_COUNT] = {
    /* DARK     */ {   0,   0,   0,   0 },  // already in dark age
    /* FEUDAL   */ { 500,   0,   0,   0 },
    /* CASTLE   */ { 800,   0, 200,   0 },
    /* IMPERIAL */ {1000,   0, 800,   0 },
};

static const int AGE_RESEARCH_TIME[AGE_COUNT] = {
    0,      // dark (N/A)
    7800,   // feudal  (~130s at 60fps)
    9600,   // castle  (~160s)
    11400,  // imperial (~190s)
};

// ---------------------------------------------------------------------------
// Name strings
// ---------------------------------------------------------------------------
static const char* const UNIT_NAMES[UNIT_TYPE_COUNT] = {
    "Villager", "Militia", "Archer", "Knight", "Spearman"
};

static const char* const BLDG_NAMES[BLDG_TYPE_COUNT] = {
    "Town Center", "House", "Barracks", "Archery Range",
    "Stable", "Farm", "Mining Camp", "Lumber Camp"
};

static const char* const AGE_NAMES[AGE_COUNT] = {
    "Dark Age", "Feudal Age", "Castle Age", "Imperial Age"
};

// ---------------------------------------------------------------------------
// Resource node data (initial amounts per tile type)
// ---------------------------------------------------------------------------
enum {
    FOREST_RESOURCE_AMT = 100,
    GOLD_RESOURCE_AMT   = 800,
    STONE_RESOURCE_AMT  = 350,
    FARM_RESOURCE_AMT   = 300,
    GATHER_CARRY_MAX    = 10,
    GATHER_RATE         = 20,   // frames per 1 unit gathered
};

// ---------------------------------------------------------------------------
// Palette indices for procedural graphics
// ---------------------------------------------------------------------------
enum {
    PAL_TRANSPARENT = 0,
    PAL_BLACK       = 1,
    PAL_BROWN       = 2,
    PAL_TAN         = 3,
    PAL_BLUE        = 4,  // player 0 color
    PAL_RED         = 5,  // player 1 color
    PAL_DARKGREEN   = 6,
    PAL_GREEN       = 7,
    PAL_YELLOW      = 8,
    PAL_GRAY        = 9,
    PAL_DARKGRAY    = 10,
    PAL_WHITE       = 11,
    PAL_ORANGE      = 12,
    PAL_LIGHTBROWN  = 13,
    PAL_SKIN        = 14,
    PAL_DARKBROWN   = 15,
};
