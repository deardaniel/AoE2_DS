#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

// ---------------------------------------------------------------------------
// Unit
// ---------------------------------------------------------------------------
struct Unit {
    bool alive;
    u8   owner;       // player ID (0 or 1)
    u8   type;        // UnitTypeId
    s16  x, y;        // pixel position (top-left of sprite)
    s16  targetX, targetY;
    s16  hp;
    u8   state;       // UnitState
    u8   direction;   // Direction
    u8   animFrame;
    u8   animTick;
    u8   gatherTick;  // separate timer for resource gathering rate
    u8   carryType;   // Resource type being carried (RES_COUNT = none)
    u8   carryAmount;
    s8   gatherTX, gatherTY; // tile coords of resource target (-1 = none)
    s8   attackTarget;       // unit index being attacked (-1 = none)
    s8   attackBldgTarget;   // building index being attacked (-1 = none)
    u8   attackCooldown;
    u8   deadTimer;          // countdown after death before removal
    u16* spriteGfx;          // OAM gfx pointer (NULL if not allocated)
    s8   oamSlot;            // OAM slot index (-1 = not visible)

    // A* path
    u8   pathLen;
    u8   pathIdx;
    u8   pathDirs[64]; // direction sequence
};

// ---------------------------------------------------------------------------
// Unit pool (global)
// ---------------------------------------------------------------------------
extern Unit units[MAX_UNITS];

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------
void units_init();
int  unit_spawn(u8 type, u8 owner, s16 px, s16 py);
void unit_kill(int idx);
void units_update(GameState& gs, TerrainMap& terrain);
void unit_command_move(int idx, s16 tx, s16 ty, TerrainMap& terrain);
void unit_command_gather(int idx, int tileTX, int tileTY, TerrainMap& terrain);
void unit_command_attack(int idx, int targetIdx);
void unit_command_attack_building(int idx, int bldgIdx);
int  unit_at_pixel(s16 px, s16 py, int ignoreOwner = -1);
int  unit_count(int owner);
int  unit_count_type(int owner, int type);
int  unit_find_idle_villager(int owner, int startFrom = 0);
int  unit_find_nearest_enemy(int unitIdx);

// A* pathfinding on tile grid
bool unit_find_path(int sx, int sy, int tx, int ty, const TerrainMap& terrain,
                    u8* outDirs, u8& outLen);
