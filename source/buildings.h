#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------
enum { MAX_GARRISON = 10 };

struct Building {
    bool alive;
    u8   owner;
    u8   type;           // BuildingTypeId
    s16  x, y;           // pixel position (top-left)
    s16  hp;
    s16  buildProgress;  // 0 = just placed, buildTime = complete
    s8   trainQueue[3];  // up to 3 queued unit types (-1 = empty)
    s16  trainProgress;  // frames into current training
    u16* spriteGfx;      // OAM gfx pointer
    s8   oamSlot;        // OAM slot (-1 = not visible)
    u8   attackCooldown; // for TC arrows
    s8   attackTargetUnit; // last unit attacked (for arrow visualization, -1 = none)
    s8   garrison[MAX_GARRISON]; // unit indices garrisoned inside (-1 = empty)
    u8   garrisonCount;
    s8   rallyTX, rallyTY;     // rally point tile (-1 = no rally)
};

// ---------------------------------------------------------------------------
// Building pool (global)
// ---------------------------------------------------------------------------
extern Building buildings[MAX_BUILDINGS];
extern u32 buildingsVersion;   // changes whenever a building appears or goes

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------
void buildings_init();
int  building_place(u8 type, u8 owner, int tileX, int tileY, GameState& gs, TerrainMap& terrain);
void buildings_update(GameState& gs, TerrainMap& terrain);
void building_damage(int idx, int amount);
void building_destroy(int idx, TerrainMap& terrain);
bool building_train(int idx, u8 unitType, GameState& gs);
void building_cancel_train(int idx, GameState& gs);
int  building_at_tile(int tx, int ty);
int  building_nearest(int owner, int type, s16 px, s16 py);
int  building_nearest_dropoff(int owner, int resType, s16 px, s16 py);
int  building_count(int owner, int type);
bool building_is_complete(int idx);
// Does building idx stop units walking on tile (tx, ty)? Not every tile of a
// footprint is solid (see buildings.cpp).
bool building_blocks_tile(int idx, int tx, int ty);
void building_complete_now(int idx);  // finish construction instantly at full HP
bool building_garrison(int bldgIdx, int unitIdx);
void building_ungarrison_all(int bldgIdx, TerrainMap& terrain);

// What a selected building's menu bar offers, in slot order: the units it
// trains, the technologies still to research there, and (Town Center) the
// next age. render.cpp draws this list and input.cpp acts on a tap, so the
// two cannot drift apart.
enum { MENU_UNIT, MENU_TECH, MENU_AGE };
struct BldgMenuItem { u8 kind, id; };
int building_menu_items(const GameState& gs, int bldgIdx, BldgMenuItem* out, int max);
// Name and cost of an item; false if it needs a later age than the player's
bool building_menu_item_info(const GameState& gs, BldgMenuItem item, const char*& name, const int*& cost);
