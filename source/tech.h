#pragma once
#include "config.h"

struct GameState;

// Tech IDs (bitfield positions)
enum TechId {
    TECH_MAN_AT_ARMS = 0, // Militia +2 HP, +1 attack (Feudal, Barracks)
    TECH_CROSSBOW    = 1, // Archer +1 range, +2 attack (Castle, Archery Range)
    TECH_CAVALIER    = 2, // Knight +20 HP, +2 attack (Imperial, Stable)
    TECH_COUNT       = 3
};

struct TechInfo {
    const char* name;
    int cost[RES_COUNT];
    int researchTime; // frames
    u8  ageReq;
    u8  bldgReq;      // BuildingTypeId where it's researched
};

static const TechInfo TECH_TABLE[TECH_COUNT] = {
    /* MAN_AT_ARMS */ { "Man-at-Arms", {100,  0, 40, 0}, 1800, AGE_FEUDAL, BLDG_BARRACKS },
    /* CROSSBOW    */ { "Crossbow",    {125,  0, 75, 0}, 2100, AGE_CASTLE, BLDG_ARCHERY_RANGE },
    /* CAVALIER    */ { "Cavalier",    {300,  0,100, 0}, 2400, AGE_IMPERIAL, BLDG_STABLE },
};

bool tech_is_researched(const GameState& gs, int player, int techId);
bool tech_start_research(GameState& gs, int player, int techId);
void tech_apply_bonuses(int player);

// Per-player modified unit stats (base + tech bonuses)
extern UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];
void tech_init_stats();
