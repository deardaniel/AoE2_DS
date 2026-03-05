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
    int      selectedUnit;   // primary selection (first selected), -1 = none
    int      selectedBldg;   // index into building pool, -1 = none
    bool     unitSelected[MAX_UNITS]; // multi-selection: true if unit i is selected
    int      selectionCount; // number of currently selected units
    GamePhase phase;
    int      frameCount;

    // Input mode
    u8  inputMode;     // 0=default, 1=placing building
    u8  placeBldgType; // which building type we're placing

    // Build menu
    bool buildMenuOpen;
    u8   buildMenuPage; // which page of building options

    // Market trading
    u8   marketTradeIdx; // 0-3: which trade pair is selected

    // Selected terrain tile (for resource info display)
    s8   selectedTileX, selectedTileY; // -1 = none

    // Drag-to-select state
    bool  touchActive;    // touch is currently held
    bool  isDragging;     // user is dragging (moved beyond threshold)
    s16   dragStartX, dragStartY; // screen coords where touch began
    s16   dragEndX, dragEndY;     // current touch position during drag

    // Move target marker (flashing diamond at destination)
    s16   moveTargetIsoX, moveTargetIsoY;
    u8    moveTargetTimer;  // frames remaining (0 = not showing)

    // Under attack alert
    u8    underAttackTimer;  // frames remaining for alert display (0 = not active)
    s16   attackAlertTX, attackAlertTY;  // tile location of attack

    // Training unit type selection (-1 = auto/first available)
    s8    trainUnitType;
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

// Selection helpers
void game_clear_selection(GameState& gs);
void game_select_unit(GameState& gs, int unitIdx);
void game_add_to_selection(GameState& gs, int unitIdx);
