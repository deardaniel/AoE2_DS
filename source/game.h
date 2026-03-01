#pragma once
#include "config.h"

// Forward declarations
struct Unit;
struct Building;
struct TerrainMap;

// ---------------------------------------------------------------------------
// Player state
// ---------------------------------------------------------------------------
struct Player {
    int resources[RES_COUNT];
    u8  age;
    u8  popCount;
    u8  popCap;
    u8  techResearched; // bitfield for upgrades (up to 8)
    s16 ageProgress;    // frames into age research (-1 = not researching)
};

// ---------------------------------------------------------------------------
// Game state enum
// ---------------------------------------------------------------------------
enum GamePhase {
    PHASE_PLAYING = 0,
    PHASE_VICTORY = 1,
    PHASE_DEFEAT  = 2,
};

// ---------------------------------------------------------------------------
// Game state — holds everything
// ---------------------------------------------------------------------------
struct GameState {
    Player   players[NUM_PLAYERS];
    int      camX, camY;
    bool     followCam;
    int      selectedUnit;   // index into unit pool, -1 = none
    int      selectedBldg;   // index into building pool, -1 = none
    GamePhase phase;
    int      frameCount;

    // Input mode
    u8  inputMode;     // 0=default, 1=placing building
    u8  placeBldgType; // which building type we're placing

    // Build menu
    bool buildMenuOpen;
    u8   buildMenuPage; // which page of building options
};

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------
void game_init(GameState& gs);
void game_update(GameState& gs);
void game_check_win_lose(GameState& gs);
bool game_can_afford(const GameState& gs, int player, const int cost[RES_COUNT]);
void game_deduct_cost(GameState& gs, int player, const int cost[RES_COUNT]);
void game_refund_cost(GameState& gs, int player, const int cost[RES_COUNT]);
void game_add_resource(GameState& gs, int player, int resType, int amount);
void game_update_pop_cap(GameState& gs, int player);
