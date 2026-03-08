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
    u8   role;        // VillagerRole (only meaningful for UNIT_VILLAGER)
    s8   gatherTX, gatherTY; // tile coords of resource target (-1 = none)
    s8   attackTarget;       // unit index being attacked (-1 = none)
    s8   attackBldgTarget;   // building index being attacked (-1 = none)
    s8   buildTarget;        // building index being constructed (-1 = none)
    s8   garrisonTarget;     // building index to garrison into (-1 = none)
    u8   attackCooldown;
    u8   waitCounter;        // frames waiting for blocked tile during movement
    u8   stance;             // UnitStance (aggressive/defensive/stand ground/no attack)
    u16  deadTimer;          // countdown after death before removal
    u16* spriteGfx;          // OAM gfx pointer (NULL if not allocated)
    s8   oamSlot;            // OAM slot index (-1 = not visible)

    // A* path
    u8   pathLen;
    u8   pathIdx;
    u8   pathDirs[64]; // direction sequence
    s8   pathDestTX, pathDestTY; // final destination tile
    s8   stepTX, stepTY;         // target tile for current path step

    // Patrol waypoints (pixel coords; patrolling = patrolAX >= 0)
    s16  patrolAX, patrolAY;     // first waypoint (-1 = not patrolling)
    s16  patrolBX, patrolBY;     // second waypoint

    // Command queue (for shift-click waypoints)
    enum { CMD_QUEUE_MAX = 4 };
    enum CmdType : u8 { CMD_NONE = 0, CMD_MOVE, CMD_ATTACK, CMD_ATTACK_BLDG, CMD_GATHER, CMD_BUILD };
    struct QueuedCmd {
        u8  type;    // CmdType
        s16 x, y;    // target position or tile coords
        s8  target;  // unit/building index for attack/build (-1 = none)
    };
    QueuedCmd cmdQueue[CMD_QUEUE_MAX];
    u8 cmdQueueLen;
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
void unit_command_build(int idx, int bldgIdx, TerrainMap& terrain);
void unit_command_garrison(int idx, int bldgIdx, TerrainMap& terrain);
void unit_command_patrol(int idx, s16 px, s16 py, TerrainMap& terrain);
int  unit_at_pixel(s16 px, s16 py, int ignoreOwner = -1);
int  unit_count(int owner);
int  unit_count_type(int owner, int type);
int  unit_find_idle_villager(int owner, int startFrom = 0);
int  unit_find_nearest_enemy(int unitIdx);

// Command queue: append a waypoint command (shift-click style)
void unit_queue_command(int idx, Unit::CmdType type, s16 x, s16 y, s8 target = -1);

// A* pathfinding on tile grid.
// selfIdx: index of the pathfinding unit (excluded from the passability map
// so it doesn't block its own start tile). Pass -1 for external callers.
bool unit_find_path(int sx, int sy, int tx, int ty, const TerrainMap& terrain,
                    u8* outDirs, u8& outLen, int selfIdx = -1, bool skipUnits = false);

// Tile occupancy — check if a tile is occupied by any alive, visible unit.
// Backed by tileOccupant grid rebuilt each frame in units_update().
bool tile_has_unit(int tx, int ty);
