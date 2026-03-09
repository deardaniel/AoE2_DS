#pragma once
#include <nds.h>

// ---------------------------------------------------------------------------
// Map
// ---------------------------------------------------------------------------
enum { MAP_TILES = 32, TILE_PX = 16, MAP_PX = MAP_TILES * TILE_PX }; // 512x512
enum { SCREEN_W = 256, SCREEN_H = 192 };

// Isometric tile dimensions (2:1 diamond shape)
enum { ISO_TILE_W = 32, ISO_TILE_H = 16 };
enum { ISO_MAP_W = MAP_TILES * ISO_TILE_W,   // 1024
       ISO_MAP_H = MAP_TILES * ISO_TILE_H };  // 512

// Terrain tile variants for visual variety
enum { GRASS_VARIANTS = 16 };
enum { DIRT_VARIANTS = 4 };

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
    TERRAIN_FARM    = 6,
    TERRAIN_BERRIES = 7,
    TERRAIN_COUNT   = 8
};

// Terrain movement speed multiplier (in 8ths: 8 = full speed, 6 = 75%)
static const u8 TERRAIN_SPEED_MULT[TERRAIN_COUNT] = {
    8,  // GRASS   — full speed
    7,  // DIRT    — slightly slower
    0,  // WATER   — impassable
    6,  // FOREST  — slow (dense trees)
    7,  // GOLD    — slightly slower (rocky)
    7,  // STONE   — slightly slower (rocky)
    8,  // FARM    — full speed
    7,  // BERRIES — slightly slower (bushes)
};

// Blend priority for terrain edge transitions (-1 = no blending)
// Higher priority bleeds into lower priority neighbors
static const s8 TERRAIN_BLEND_PRIORITY[TERRAIN_COUNT] = {
    1,  // GRASS
    2,  // DIRT — bleeds into grass
    0,  // WATER — lowest
   -1,  // FOREST — no blending
   -1,  // GOLD
   -1,  // STONE
   -1,  // FARM
   -1,  // BERRIES — no blending
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
    UNIT_SCOUT     = 5,
    UNIT_SHEEP     = 6,
    UNIT_RAM       = 7,
    UNIT_MANGONEL  = 8,
    UNIT_MONK      = 9,
    UNIT_TYPE_COUNT = 10
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
    BLDG_WALL          = 8,
    BLDG_TOWER         = 9,
    BLDG_MARKET        = 10,
    BLDG_CASTLE        = 11,
    BLDG_MONASTERY     = 12,
    BLDG_TYPE_COUNT    = 13
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
    USTATE_BUILDING   = 5,
    USTATE_DEAD       = 6,
    USTATE_SCOUTING   = 7,
    USTATE_GARRISONED = 8
};

// ---------------------------------------------------------------------------
// Villager roles (determines sprite sheet used)
// ---------------------------------------------------------------------------
enum VillagerRole {
    VROLE_BASE       = 0,
    VROLE_LUMBERJACK = 1,
    VROLE_MINER      = 2,
    VROLE_BUILDER    = 3,
    VROLE_FARMER     = 4,
    VROLE_FORAGER    = 5,
    VROLE_COUNT      = 6
};

// Unit stance controls auto-engage behavior
enum UnitStance {
    STANCE_AGGRESSIVE = 0, // Attack anything in LOS, chase indefinitely
    STANCE_DEFENSIVE  = 1, // Attack in LOS but return to position if target flees
    STANCE_STAND      = 2, // Attack in range only, never move to engage
    STANCE_NO_ATTACK  = 3, // Never auto-engage
};

// ---------------------------------------------------------------------------
// Directions (8 directions for proper isometric sprite mapping)
// ---------------------------------------------------------------------------
enum Direction {
    DIR_N  = 0,   // -ty          (upper-right on screen)
    DIR_NE = 1,   // +tx, -ty     (right on screen)
    DIR_E  = 2,   // +tx          (lower-right on screen)
    DIR_SE = 3,   // +tx, +ty     (down on screen)
    DIR_S  = 4,   // +ty          (lower-left on screen)
    DIR_SW = 5,   // -tx, +ty     (left on screen)
    DIR_W  = 6,   // -tx          (upper-left on screen)
    DIR_NW = 7,   // -tx, -ty     (up on screen)
    DIR_COUNT = 8
};

// ---------------------------------------------------------------------------
// Damage classes (simplified from AoE2's full system)
// Each unit has attack values per class and armor values per class.
// Damage = sum of max(attack[class] - armor[class], 0) for each class, min 1.
// ---------------------------------------------------------------------------
enum DamageClass {
    DMG_MELEE    = 0,  // Class 4 in AoE2 — base melee damage
    DMG_PIERCE   = 1,  // Class 3 in AoE2 — ranged/arrow damage
    DMG_BONUS_CAV = 2, // Class 8 in AoE2 — bonus vs cavalry
    DMG_BONUS_INF = 3, // Class 1 in AoE2 — bonus vs infantry (unique units etc.)
    DMG_CLASS_COUNT = 4
};

// Unit class tags for bonus damage matching
enum UnitClass {
    UCLASS_NONE     = 0,
    UCLASS_INFANTRY = 1,
    UCLASS_CAVALRY  = 2,
};

// Which unit types belong to which class (for bonus damage)
// Unit class tags
enum {
    UCLASS_SIEGE    = 3,
};

static const u8 UNIT_CLASS[UNIT_TYPE_COUNT] = {
    /* VILLAGER  */ UCLASS_NONE,
    /* MILITIA   */ UCLASS_INFANTRY,
    /* ARCHER    */ UCLASS_NONE,      // archers are "archer" class but we don't have bonus vs archer
    /* KNIGHT    */ UCLASS_CAVALRY,
    /* SPEARMAN  */ UCLASS_INFANTRY,
    /* SCOUT     */ UCLASS_CAVALRY,
    /* SHEEP     */ UCLASS_NONE,
    /* RAM       */ UCLASS_SIEGE,
    /* MANGONEL  */ UCLASS_SIEGE,
    /* MONK      */ UCLASS_NONE,
};

// Calculate total damage from attacker stats vs defender stats
// Sums max(atk[class] - def[class], 0) for each class, min total 1
// Only applies bonus_cav if defender is cavalry, bonus_inf if infantry
static inline int calc_damage(const s16 atk[DMG_CLASS_COUNT],
                               const s16 def[DMG_CLASS_COUNT],
                               u8 defenderType) {
    int total = 0;
    for (int c = 0; c < DMG_CLASS_COUNT; c++) {
        if (atk[c] <= 0) continue;
        // Only apply bonus damage if defender has matching class
        if (c == DMG_BONUS_CAV && UNIT_CLASS[defenderType] != UCLASS_CAVALRY) continue;
        if (c == DMG_BONUS_INF && UNIT_CLASS[defenderType] != UCLASS_INFANTRY) continue;
        int d = atk[c] - def[c];
        if (d > 0) total += d;
    }
    return (total > 0) ? total : 1;
}

// ---------------------------------------------------------------------------
// Unit stat table
// ---------------------------------------------------------------------------
struct UnitStats {
    s16 hp;
    s16 attack[DMG_CLASS_COUNT];  // damage per class
    s16 armor[DMG_CLASS_COUNT];   // armor per class
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
// - Attack/Armor: per damage class {melee, pierce, bonus_cav, bonus_inf}
//                                hp  atk{mel,prc,cav,inf} arm{mel,prc,cav,inf} rng spd los  train   F    W    G    S   age  bldg
static const UnitStats UNIT_STATS[UNIT_TYPE_COUNT] = {
    /* VILLAGER  */ {  25, {3,0,0,0}, {0,0,0,0},  1,  1,  4, 1500, { 50,  0,  0,  0}, AGE_DARK,   BLDG_TOWN_CENTER },
    /* MILITIA   */ {  40, {4,0,0,0}, {0,1,0,0},  1,  1,  4, 1260, { 60,  0, 20,  0}, AGE_DARK,   BLDG_BARRACKS },
    /* ARCHER    */ {  30, {0,4,0,0}, {0,0,0,0},  4,  1,  6, 2100, {  0, 25, 45,  0}, AGE_FEUDAL, BLDG_ARCHERY_RANGE },
    /* KNIGHT    */ { 100, {10,0,0,0},{2,2,0,0},  1,  2,  4, 1800, { 60,  0, 75,  0}, AGE_CASTLE, BLDG_STABLE },
    /* SPEARMAN  */ {  45, {3,0,15,0},{0,0,0,0},  1,  1,  4, 1320, { 35, 25,  0,  0}, AGE_FEUDAL, BLDG_BARRACKS },
    /* SCOUT     */ {  45, {5,0,0,0}, {0,2,0,0},  1,  2,  6,    0, {  0,  0,  0,  0}, AGE_DARK,   BLDG_STABLE },
    /* SHEEP     */ {  25, {0,0,0,0}, {0,0,0,0},  0,  1,  2,    0, {  0,  0,  0,  0}, AGE_DARK,   BLDG_TYPE_COUNT },
    /* RAM       */ { 175, {2,0,0,0},{-3,180,0,0}, 1,  1,  3, 2400, {  0,  0,  0, 75}, AGE_CASTLE, BLDG_CASTLE },
    /* MANGONEL  */ {  50, {0,40,0,0},{0,6,0,0},   7,  1,  7, 2700, {  0, 160, 0,  0}, AGE_CASTLE, BLDG_CASTLE },
    /* MONK      */ {  30, {0,0,0,0},{0,0,0,0},   9,  1,  7, 3600, {  0,   0,100, 0}, AGE_CASTLE, BLDG_MONASTERY },
};

// ---------------------------------------------------------------------------
// Data tables — building stats
// ---------------------------------------------------------------------------
// Building stats based on real AoE2 values (wiki-verified), scaled for DSi:
// - HP scaled down (real TC = 2400, too high for DSi combat pace)
// - Build times in frames at 60fps
//                                      hp  build   F    W    G    S   age  pop  tw th
static const BuildingStats BLDG_STATS[BLDG_TYPE_COUNT] = {
    /* TOWN_CENTER   */ { 600, 600, {  0, 275,  0, 100}, AGE_DARK,    5,  4, 4 },
    /* HOUSE         */ { 150, 150, {  0,  25,  0,   0}, AGE_DARK,    5,  1, 1 },
    /* BARRACKS      */ { 350, 300, {  0, 175,  0,   0}, AGE_DARK,    0,  2, 2 },
    /* ARCHERY_RANGE */ { 350, 300, {  0, 175,  0,   0}, AGE_FEUDAL,  0,  2, 2 },
    /* STABLE        */ { 350, 300, {  0, 175,  0,   0}, AGE_CASTLE,  0,  2, 2 },
    /* FARM          */ {  50, 100, {  0,  60,  0,   0}, AGE_DARK,    0,  1, 1 },
    /* MINING_CAMP   */ { 100, 150, {  0, 100,  0,   0}, AGE_DARK,    0,  1, 1 },
    /* LUMBER_CAMP   */ { 100, 150, {  0, 100,  0,   0}, AGE_DARK,    0,  1, 1 },
    /* WALL          */ { 100,  30, {  0,   5,  0,   0}, AGE_DARK,    0,  1, 1 },
    /* TOWER         */ { 200, 300, {  0,  50,  0,  25}, AGE_FEUDAL,  0,  1, 1 },
    /* MARKET        */ { 350, 300, {  0, 175,  0,   0}, AGE_FEUDAL,  0,  2, 2 },
    /* CASTLE        */ {1000, 900, {  0,   0,  0, 650}, AGE_IMPERIAL,0,  3, 3 },
    /* MONASTERY     */ { 350, 300, {  0, 175,  0,   0}, AGE_CASTLE,  0,  2, 2 },
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
    "Villager", "Militia", "Archer", "Knight", "Spearman", "Scout", "Sheep",
    "Bat. Ram", "Mangonel", "Monk"
};

static const char* const BLDG_NAMES[BLDG_TYPE_COUNT] = {
    "Town Center", "House", "Barracks", "Archery Range",
    "Stable", "Farm", "Mining Camp", "Lumber Camp",
    "Wall", "Tower", "Market", "Castle", "Monastery"
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
    BERRIES_RESOURCE_AMT = 125,
    SHEEP_FOOD_AMOUNT   = 100,  // food gained when villager gathers from sheep unit
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
