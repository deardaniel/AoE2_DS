#pragma once
#include "config.h"

struct GameState;

// Tech IDs (bitfield positions)
enum TechId {
    TECH_MAN_AT_ARMS  = 0, // Militia +2 HP, +1 attack (Feudal, Barracks)
    TECH_CROSSBOW     = 1, // Archer +1 range, +2 attack (Castle, Archery Range)
    TECH_CAVALIER     = 2, // Knight +20 HP, +2 attack (Imperial, Stable)
    TECH_LOOM         = 3, // Villager +15 HP, +1 armor (Dark, TC)
    TECH_DOUBLE_BIT   = 4, // Wood gathering +20% (Feudal, Lumber Camp)
    TECH_WHEELBARROW  = 5, // Villager speed +1, carry +5 (Feudal, TC)
    TECH_GOLD_MINING  = 6, // Gold gathering +15% (Feudal, Mining Camp)
    TECH_STONE_MINING = 7, // Stone gathering +15% (Feudal, Mining Camp)
    TECH_COUNT        = 8
};

struct TechInfo {
    const char* name;
    int cost[RES_COUNT];
    int researchTime; // frames
    u8  ageReq;
    u8  bldgReq;      // BuildingTypeId where it's researched
};

static const TechInfo TECH_TABLE[TECH_COUNT] = {
    /* MAN_AT_ARMS   */ { "Man-at-Arms", {100,  0, 40, 0}, 1800, AGE_FEUDAL,   BLDG_BARRACKS },
    /* CROSSBOW      */ { "Crossbow",    {125,  0, 75, 0}, 2100, AGE_CASTLE,   BLDG_ARCHERY_RANGE },
    /* CAVALIER      */ { "Cavalier",    {300,  0,100, 0}, 2400, AGE_IMPERIAL, BLDG_STABLE },
    /* LOOM          */ { "Loom",        {  0,  0, 50, 0},  900, AGE_DARK,     BLDG_TOWN_CENTER },
    /* DOUBLE_BIT    */ { "DoubleBitAxe",{100,  0, 50, 0}, 1200, AGE_FEUDAL,   BLDG_LUMBER_CAMP },
    /* WHEELBARROW   */ { "Wheelbarrow", {175, 50,  0, 0}, 1200, AGE_FEUDAL,   BLDG_TOWN_CENTER },
    /* GOLD_MINING   */ { "Gold Mining", {100, 75,  0, 0}, 1200, AGE_FEUDAL,   BLDG_MINING_CAMP },
    /* STONE_MINING  */ { "Stone Mining",{100, 75,  0, 0}, 1200, AGE_FEUDAL,   BLDG_MINING_CAMP },
};

bool tech_is_researched(const GameState& gs, int player, int techId);
bool tech_start_research(GameState& gs, int player, int techId);
void tech_apply_bonuses(int player);

// Per-player modified unit stats (base + tech bonuses)
extern UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];

// Per-player economy bonuses (set by tech_apply_bonuses)
extern u8 playerGatherRate[NUM_PLAYERS][RES_COUNT]; // frames per gather tick per resource
extern u8 playerCarryMax[NUM_PLAYERS];              // max carry amount

void tech_init_stats();
