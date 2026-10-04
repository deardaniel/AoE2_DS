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
    u32 techResearched; // bitfield for upgrades (up to 32)
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

enum AIDifficulty {
    AI_EASY   = 0,
    AI_NORMAL = 1,
    AI_HARD   = 2,
};

// ---------------------------------------------------------------------------
// Game state — holds everything
// ---------------------------------------------------------------------------
enum { MARK_POINT, MARK_TILE, MARK_BUILDING, MARK_UNIT };
enum { MARK_FRAMES = 36 };

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
    // What the last order pointed at, shown for a moment so a tap can be
    // seen to have landed: a spot on the ground (moveTargetIso), a tile, a
    // building or a unit (markRef), in markColour
    u8    markKind;         // MARK_*
    u8    markColour;
    s16   markRef, markRef2;

    // Under attack alert
    u8    underAttackTimer;  // frames remaining for alert display (0 = not active)
    s16   attackAlertTX, attackAlertTY;  // tile location of attack

    // Training unit type selection (-1 = auto/first available)
    s8    trainUnitType;
    // Last technology (MENU_TECH) or age (MENU_AGE) slot tapped, for the cost
    // readout; menuKind < 0 = none
    s8    menuKind, menuId;

    // AI difficulty
    u8    aiDifficulty;  // AIDifficulty enum

    // End-game stats
    u16   unitsKilled[NUM_PLAYERS];   // enemy units killed by each player
    u16   unitsLost[NUM_PLAYERS];     // units lost by each player
    u16   bldgsDestroyed[NUM_PLAYERS]; // enemy buildings destroyed
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
