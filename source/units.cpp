#include "units.h"
#include "game.h"
#include "buildings.h"
#include "terrain.h"
#include "tech.h"
#include "fog.h"
#include "sound.h"
#include "projectiles.h"
#include <string.h>

Unit units[MAX_UNITS];

// ---------------------------------------------------------------------------
// Tile occupancy grid — unit collision avoidance system
//
// Each cell holds the index of the unit occupying that tile, or -1 if empty.
// Rebuilt at the start of every units_update() frame. Used by:
//   - unit_step_path(): block movement into occupied friendly tiles
//   - build_pass_map(): A* routes around stationary units
//   - buildings.cpp:    spawn/ungarrison onto unoccupied tiles only
//   - unit_command_gather/build(): prefer unoccupied adjacent tiles
// ---------------------------------------------------------------------------
static s8 tileOccupant[MAP_TILES][MAP_TILES];

// Rebuild the occupancy grid from scratch. Only alive, visible units are
// tracked (dead and garrisoned units don't occupy map space).
static void rebuild_occupancy() {
    memset(tileOccupant, -1, sizeof(tileOccupant));
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD || units[i].state == USTATE_GARRISONED) continue;
        int tx = units[i].x / TILE_PX;
        int ty = units[i].y / TILE_PX;
        if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES)
            tileOccupant[ty][tx] = i;
    }
}

// Check whether a map tile is currently occupied by any unit.
// Used externally by buildings.cpp for spawn/ungarrison placement.
bool tile_has_unit(int tx, int ty) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return false;
    return tileOccupant[ty][tx] >= 0;
}

void units_init() {
    memset(units, 0, sizeof(units));
    for (int i = 0; i < MAX_UNITS; i++) {
        units[i].alive = false;
        units[i].attackTarget = -1;
        units[i].attackBldgTarget = -1;
        units[i].buildTarget = -1;
        units[i].garrisonTarget = -1;
        units[i].gatherTX = -1;
        units[i].gatherTY = -1;
        units[i].oamSlot = -1;
        units[i].carryType = RES_COUNT;
        units[i].role = VROLE_BASE;
        units[i].cmdQueueLen = 0;
        units[i].patrolAX = -1;
    }
}

int unit_spawn(u8 type, u8 owner, s16 px, s16 py) {
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive) {
            Unit& u = units[i];
            u.alive = true;
            u.owner = owner;
            u.type = type;
            u.x = px; u.y = py;
            u.targetX = px; u.targetY = py;
            u.hp = playerUnitStats[owner][type].hp;
            u.state = USTATE_IDLE;
            u.direction = DIR_S;
            u.animFrame = 0;
            u.animTick = 0;
            u.gatherTick = 0;
            u.carryType = RES_COUNT;
            u.carryAmount = 0;
            u.role = VROLE_BASE;
            u.gatherTX = -1; u.gatherTY = -1;
            u.attackTarget = -1;
            u.attackBldgTarget = -1;
            u.buildTarget = -1;
            u.garrisonTarget = -1;
            u.attackCooldown = 0;
            u.waitCounter = 0;
            u.stance = STANCE_AGGRESSIVE;
            u.deadTimer = 0;
            u.spriteGfx = NULL;
            u.oamSlot = -1;
            u.pathLen = 0;
            u.pathIdx = 0;
            u.cmdQueueLen = 0;
            u.patrolAX = -1;
            return i;
        }
    }
    return -1;
}

void unit_kill(int idx) {
    if (idx < 0 || idx >= MAX_UNITS) return;
    Unit& u = units[idx];
    // Track end-game stats
    extern GameState gameState;
    gameState.unitsLost[u.owner]++;
    // The opposing player gets credit for the kill
    gameState.unitsKilled[1 - u.owner]++;
    u.state = USTATE_DEAD;
    u.deadTimer = 300; // ~5 seconds: death animation + corpse linger
    u.animFrame = 0;
    u.animTick = 0;
    sound_play(SFX_UNIT_DEATH);
}

// ---------------------------------------------------------------------------
// A* Pathfinding on 32x32 tile grid
// ---------------------------------------------------------------------------
struct AStarNode {
    s16 g, f;
    s8  parentX, parentY;
    bool open, closed;
};

static AStarNode astarGrid[MAP_TILES][MAP_TILES];
static bool passMap[MAP_TILES][MAP_TILES]; // combined terrain + building passability
static s16 openList[MAP_TILES * MAP_TILES]; // encoded as y * MAP_TILES + x

// Direction offsets (8-directional)
static const s8 DX8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
static const s8 DY8[8] = {-1,-1, 0, 1, 1, 1, 0,-1};

// 8-dir path indices map directly to Direction enum (both use N,NE,E,SE,S,SW,W,NW order)

// Convert dx/dy delta to an 8-direction enum value
static Direction dir_from_delta(int dx, int dy) {
    // Normalize to -1/0/+1
    int sx = (dx > 0) ? 1 : (dx < 0) ? -1 : 0;
    int sy = (dy > 0) ? 1 : (dy < 0) ? -1 : 0;
    // Look up in DX8/DY8 table
    for (int d = 0; d < 8; d++) {
        if (DX8[d] == sx && DY8[d] == sy) return (Direction)d;
    }
    return DIR_S; // fallback
}

static int heuristic(int ax, int ay, int bx, int by) {
    int dx = ax - bx; if (dx < 0) dx = -dx;
    int dy = ay - by; if (dy < 0) dy = -dy;
    return (dx > dy) ? dx : dy; // Chebyshev distance
}

// Build combined passability map for A* pathfinding.
// Layers: terrain → buildings → stationary units.
// selfIdx excludes the pathfinding unit from the blocked set so it doesn't
// block its own starting tile. Moving/scouting units are also excluded
// since they'll likely clear their tile before the pathing unit arrives.
// skipUnits: if true, don't mark any units as blockers (used for scouts).
static void build_pass_map(const TerrainMap& terrain, int selfIdx = -1, bool skipUnits = false, int friendlyPlayer = -1) {
    for (int y = 0; y < MAP_TILES; y++)
        for (int x = 0; x < MAP_TILES; x++)
            passMap[y][x] = terrain.passable(x, y);

    // Mark building tiles as impassable
    // Friendly walls act as gates — passable for the owning player
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        // Friendly walls are passable (gate mechanic)
        if (buildings[i].type == BLDG_WALL && buildings[i].owner == friendlyPlayer)
            continue;
        int bx = buildings[i].x / TILE_PX;
        int by = buildings[i].y / TILE_PX;
        int bw = BLDG_STATS[buildings[i].type].tileW;
        int bh = BLDG_STATS[buildings[i].type].tileH;
        for (int dy = 0; dy < bh; dy++)
            for (int dx = 0; dx < bw; dx++) {
                int tx = bx + dx, ty = by + dy;
                if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES)
                    passMap[ty][tx] = false;
            }
    }

    // Mark stationary units as impassable (skip self and moving/scouting units)
    if (!skipUnits) {
        for (int i = 0; i < MAX_UNITS; i++) {
            if (i == selfIdx) continue;
            if (!units[i].alive || units[i].state == USTATE_DEAD || units[i].state == USTATE_GARRISONED) continue;
            if (units[i].state == USTATE_MOVING || units[i].state == USTATE_SCOUTING) continue;
            int tx = units[i].x / TILE_PX, ty = units[i].y / TILE_PX;
            if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES)
                passMap[ty][tx] = false;
        }
    }
}

bool unit_find_path(int sx, int sy, int tx, int ty, const TerrainMap& terrain,
                    u8* outDirs, u8& outLen, int selfIdx, bool skipUnits) {
    outLen = 0;
    if (sx == tx && sy == ty) return true;

    // Clamp to map
    if (tx < 0) tx = 0;
    if (tx >= MAP_TILES) tx = MAP_TILES - 1;
    if (ty < 0) ty = 0;
    if (ty >= MAP_TILES) ty = MAP_TILES - 1;

    // Infer friendly player from the pathfinding unit (for gate mechanic)
    int friendlyPlayer = -1;
    if (selfIdx >= 0 && selfIdx < MAX_UNITS && units[selfIdx].alive)
        friendlyPlayer = units[selfIdx].owner;

    // Build combined passability map (terrain + buildings + optionally units)
    // friendlyPlayer's walls are passable (gate mechanic)
    build_pass_map(terrain, selfIdx, skipUnits, friendlyPlayer);

    // Ensure start tile is passable (unit might be on a building tile)
    passMap[sy][sx] = true;

    // If target is impassable, find nearest passable neighbor
    if (!passMap[ty][tx]) {
        bool found = false;
        for (int r = 1; r <= 5 && !found; r++) {
            for (int d = 0; d < 8; d++) {
                int nx = tx + DX8[d] * r;
                int ny = ty + DY8[d] * r;
                if (nx >= 0 && nx < MAP_TILES && ny >= 0 && ny < MAP_TILES && passMap[ny][nx]) {
                    tx = nx; ty = ny;
                    found = true;
                    break;
                }
            }
        }
        if (!found) return false;
    }

    // Init grid
    memset(astarGrid, 0, sizeof(astarGrid));
    for (int y = 0; y < MAP_TILES; y++)
        for (int x = 0; x < MAP_TILES; x++)
            astarGrid[y][x].parentX = astarGrid[y][x].parentY = -1;

    int openCount = 0;

    astarGrid[sy][sx].g = 0;
    astarGrid[sy][sx].f = heuristic(sx, sy, tx, ty) * 10;
    astarGrid[sy][sx].open = true;
    openList[openCount++] = sy * MAP_TILES + sx;

    for (int iterations = 0; iterations < 1024; iterations++) {
        if (openCount == 0) return false; // no path

        // Find open node with lowest f in the open list
        int bestListIdx = 0;
        int bestF = astarGrid[openList[0] / MAP_TILES][openList[0] % MAP_TILES].f;
        for (int i = 1; i < openCount; i++) {
            int y = openList[i] / MAP_TILES;
            int x = openList[i] % MAP_TILES;
            if (astarGrid[y][x].f < bestF) {
                bestF = astarGrid[y][x].f;
                bestListIdx = i;
            }
        }

        int bestPos = openList[bestListIdx];
        int bestX = bestPos % MAP_TILES;
        int bestY = bestPos / MAP_TILES;

        // Swap-remove from open list
        openList[bestListIdx] = openList[--openCount];

        if (bestX == tx && bestY == ty) {
            // Reconstruct path
            u8 tempDirs[64];
            int len = 0;
            int cx = tx, cy = ty;
            while (!(cx == sx && cy == sy) && len < 64) {
                int px = astarGrid[cy][cx].parentX;
                int py = astarGrid[cy][cx].parentY;
                int dx = cx - px;
                int dy = cy - py;
                u8 dir = 0;
                for (int d = 0; d < 8; d++) {
                    if (DX8[d] == dx && DY8[d] == dy) { dir = d; break; }
                }
                tempDirs[len++] = dir;
                cx = px; cy = py;
            }
            // Reverse into output
            outLen = len;
            for (int i = 0; i < len; i++) {
                outDirs[i] = tempDirs[len - 1 - i];
            }
            return true;
        }

        astarGrid[bestY][bestX].open = false;
        astarGrid[bestY][bestX].closed = true;

        for (int d = 0; d < 8; d++) {
            int nx = bestX + DX8[d];
            int ny = bestY + DY8[d];
            if (nx < 0 || nx >= MAP_TILES || ny < 0 || ny >= MAP_TILES) continue;
            if (astarGrid[ny][nx].closed) continue;
            if (!passMap[ny][nx]) continue;

            // Diagonal: check corner-cutting (don't cut through obstacles)
            if (DX8[d] != 0 && DY8[d] != 0) {
                if (!passMap[bestY][bestX + DX8[d]] || !passMap[bestY + DY8[d]][bestX]) continue;
            }

            int ng = astarGrid[bestY][bestX].g + ((DX8[d] != 0 && DY8[d] != 0) ? 14 : 10);
            if (!astarGrid[ny][nx].open || ng < astarGrid[ny][nx].g) {
                astarGrid[ny][nx].g = ng;
                astarGrid[ny][nx].f = ng + heuristic(nx, ny, tx, ty) * 10;
                astarGrid[ny][nx].parentX = bestX;
                astarGrid[ny][nx].parentY = bestY;
                if (!astarGrid[ny][nx].open) {
                    astarGrid[ny][nx].open = true;
                    openList[openCount++] = ny * MAP_TILES + nx;
                }
            }
        }
    }

    return false;
}

// Forward declaration
static void unit_begin_path(Unit& u, int sx, int sy, int tx, int ty);

// ---------------------------------------------------------------------------
// Nudge: push an idle friendly unit out of the way
//
// When a moving unit needs to step into a tile occupied by an idle friendly,
// the idle unit is given a 1-step path to the nearest empty adjacent tile.
// This prevents permanent blockages where idle units clog chokepoints.
// ---------------------------------------------------------------------------
static void nudge_unit(int idx, int fromTX, int fromTY, const TerrainMap& terrain) {
    Unit& other = units[idx];
    int ox = other.x / TILE_PX;
    int oy = other.y / TILE_PX;
    // Find nearest empty adjacent tile
    for (int d = 0; d < 8; d++) {
        int nx = ox + DX8[d];
        int ny = oy + DY8[d];
        if (nx < 0 || nx >= MAP_TILES || ny < 0 || ny >= MAP_TILES) continue;
        if (!terrain.passable(nx, ny)) continue;
        if (tileOccupant[ny][nx] >= 0) continue;
        // Set a 1-step path
        other.pathDirs[0] = d;
        other.pathLen = 1;
        other.pathIdx = 0;
        other.pathDestTX = nx;
        other.pathDestTY = ny;
        other.stepTX = nx;
        other.stepTY = ny;
        other.state = USTATE_MOVING;
        other.waitCounter = 0;
        return;
    }
}

// ---------------------------------------------------------------------------
// Movement along path with collision avoidance
//
// Uses pre-computed step target (stepTX, stepTY) to avoid drift from
// recalculating the target tile each frame based on current position.
//
// Collision rules (checked before snapping to next tile):
//   1. Friendly idle occupant → nudge it away, then wait
//   2. Friendly moving occupant → wait (it will clear on its own)
//   3. After 8 frames of waiting → repath around the blocker
//   4. Enemy occupant → step through (combat resolves overlap)
// ---------------------------------------------------------------------------
static void unit_step_path(Unit& u, int selfIdx, const TerrainMap& terrain) {
    if (u.pathIdx >= u.pathLen) {
        // Snap to final destination and stop
        u.x = u.pathDestTX * TILE_PX;
        u.y = u.pathDestTY * TILE_PX;
        u.state = USTATE_IDLE;
        u.pathLen = 0;
        u.waitCounter = 0;
        return;
    }

    int speed = playerUnitStats[u.owner][u.type].speed;
    // Apply terrain speed multiplier based on current tile
    {
        int utx = u.x / TILE_PX, uty = u.y / TILE_PX;
        if (utx >= 0 && utx < MAP_TILES && uty >= 0 && uty < MAP_TILES) {
            u8 tt = terrain.tileAt(utx, uty);
            speed = speed * TERRAIN_SPEED_MULT[tt] / 8;
            if (speed < 1) speed = 1;
        }
    }
    int targetPX = u.stepTX * TILE_PX;
    int targetPY = u.stepTY * TILE_PX;

    int dx = targetPX - u.x;
    int dy = targetPY - u.y;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;

    // Collision check before stepping into next tile
    if (adx <= speed && ady <= speed) {
        int nextTX = u.stepTX;
        int nextTY = u.stepTY;
        if (nextTX >= 0 && nextTX < MAP_TILES && nextTY >= 0 && nextTY < MAP_TILES) {
            int occupant = tileOccupant[nextTY][nextTX];
            if (occupant >= 0 && occupant != selfIdx) {
                Unit& other = units[occupant];
                if (other.owner == u.owner && u.state != USTATE_SCOUTING) {
                    if (other.state == USTATE_IDLE) {
                        // Nudge idle unit out of the way
                        nudge_unit(occupant, nextTX, nextTY, terrain);
                    }
                    // Wait for tile to clear
                    u.waitCounter++;
                    if (u.waitCounter >= 8) {
                        // Repath around blocker
                        u.waitCounter = 0;
                        int sx = u.x / TILE_PX;
                        int sy = u.y / TILE_PX;
                        if (unit_find_path(sx, sy, u.pathDestTX, u.pathDestTY, terrain,
                                           u.pathDirs, u.pathLen, selfIdx)) {
                            unit_begin_path(u, sx, sy, u.pathDestTX, u.pathDestTY);
                        } else {
                            u.state = USTATE_IDLE;
                            u.pathLen = 0;
                        }
                    }
                    return; // don't step this frame
                }
                // Enemy: step anyway (overlap acceptable in combat)
            }
        }
        u.waitCounter = 0;

        // Close enough — snap to tile origin and advance path
        u.x = targetPX;
        u.y = targetPY;
        u.pathIdx++;
        // Compute next step's target tile
        if (u.pathIdx < u.pathLen) {
            u.stepTX += DX8[u.pathDirs[u.pathIdx]];
            u.stepTY += DY8[u.pathDirs[u.pathIdx]];
        }
    } else {
        // Move toward target (each axis independently to prevent overshoot)
        if (adx > speed) u.x += (dx > 0) ? speed : -speed;
        else u.x = targetPX;
        if (ady > speed) u.y += (dy > 0) ? speed : -speed;
        else u.y = targetPY;
    }

    // Clamp to map
    if (u.x < 0) u.x = 0;
    if (u.y < 0) u.y = 0;
    if (u.x > MAP_PX - TILE_PX) u.x = MAP_PX - TILE_PX;
    if (u.y > MAP_PX - TILE_PX) u.y = MAP_PX - TILE_PX;

    // Direction from current path step
    u8 dirIdx = (u.pathIdx < u.pathLen) ? u.pathIdx : u.pathLen - 1;
    u.direction = (Direction)u.pathDirs[dirIdx];
}

// ---------------------------------------------------------------------------
// Helper: initialize path state after unit_find_path succeeds
// ---------------------------------------------------------------------------
static void unit_begin_path(Unit& u, int sx, int sy, int tx, int ty) {
    u.pathIdx = 0;
    u.pathDestTX = tx;
    u.pathDestTY = ty;
    // Compute first step target tile
    if (u.pathLen > 0) {
        u.stepTX = sx + DX8[u.pathDirs[0]];
        u.stepTY = sy + DY8[u.pathDirs[0]];
    }
}

// ---------------------------------------------------------------------------
// Helper: find nearest resource tile of same type as carry
// ---------------------------------------------------------------------------
static bool find_nearest_resource(int cx, int cy, u8 carryType, const TerrainMap& terrain,
                                  int& outTX, int& outTY) {
    // Map carry type to terrain type(s)
    int bestDist = 99999;
    outTX = -1;
    outTY = -1;
    for (int ty = 0; ty < MAP_TILES; ty++) {
        for (int tx = 0; tx < MAP_TILES; tx++) {
            u8 tt = terrain.tileAt(tx, ty);
            bool match = false;
            if (carryType == RES_WOOD  && tt == TERRAIN_FOREST) match = true;
            if (carryType == RES_GOLD  && tt == TERRAIN_GOLD)   match = true;
            if (carryType == RES_STONE && tt == TERRAIN_STONE)  match = true;
            if (carryType == RES_FOOD  && tt == TERRAIN_FARM)   match = true;
            if (carryType == RES_FOOD  && tt == TERRAIN_BERRIES) match = true;
            if (!match) continue;
            int dx = tx - cx; if (dx < 0) dx = -dx;
            int dy = ty - cy; if (dy < 0) dy = -dy;
            int dist = dx + dy; // Manhattan distance
            if (dist < bestDist) {
                bestDist = dist;
                outTX = tx;
                outTY = ty;
            }
        }
    }
    return outTX >= 0;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------
void unit_command_move(int idx, s16 px, s16 py, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    Unit& u = units[idx];
    u.cmdQueueLen = 0; // new direct command clears queue
    u.patrolAX = -1;   // cancel patrol

    int sx = u.x / TILE_PX;
    int sy = u.y / TILE_PX;
    int tx = px / TILE_PX;
    int ty = py / TILE_PX;

    if (unit_find_path(sx, sy, tx, ty, terrain, u.pathDirs, u.pathLen, idx)) {
        unit_begin_path(u, sx, sy, tx, ty);
        u.state = USTATE_MOVING;
        u.attackTarget = -1;
        u.attackBldgTarget = -1;
        u.buildTarget = -1;
        u.garrisonTarget = -1;
        u.gatherTX = -1;
        u.gatherTY = -1;
        // Reset role to base when given explicit move command
        if (u.type == UNIT_VILLAGER && u.carryAmount == 0) {
            u.role = VROLE_BASE;
        }
    }
}

void unit_command_gather(int idx, int tileTX, int tileTY, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    Unit& u = units[idx];
    if (u.type != UNIT_VILLAGER) return;
    u.cmdQueueLen = 0;
    u.patrolAX = -1;

    u8 tt = terrain.tileAt(tileTX, tileTY);
    if (tt != TERRAIN_FOREST && tt != TERRAIN_GOLD && tt != TERRAIN_STONE &&
        tt != TERRAIN_FARM && tt != TERRAIN_BERRIES)
        return;

    u.gatherTX = tileTX;
    u.gatherTY = tileTY;
    u.attackTarget = -1;
    u.buildTarget = -1;
    u.carryType = RES_COUNT;
    u.carryAmount = 0;

    // Determine carry type and role from terrain
    if (tt == TERRAIN_FOREST) { u.carryType = RES_WOOD;  u.role = VROLE_LUMBERJACK; }
    else if (tt == TERRAIN_GOLD)   { u.carryType = RES_GOLD;  u.role = VROLE_MINER; }
    else if (tt == TERRAIN_STONE)  { u.carryType = RES_STONE; u.role = VROLE_MINER; }
    else if (tt == TERRAIN_FARM)    { u.carryType = RES_FOOD;  u.role = VROLE_FARMER; }
    else if (tt == TERRAIN_BERRIES) { u.carryType = RES_FOOD;  u.role = VROLE_FORAGER; }

    // Find path to nearest adjacent passable tile of the resource
    int sx = u.x / TILE_PX;
    int sy = u.y / TILE_PX;

    // Try adjacent tiles sorted by distance, preferring unoccupied tiles.
    // The +10000 penalty ensures occupied tiles sort after all unoccupied ones,
    // so multiple villagers gathering the same resource spread to different tiles.
    bool pathed = false;
    int adjOrder[8];
    int adjDist[8];
    for (int d = 0; d < 8; d++) {
        adjOrder[d] = d;
        int ax = tileTX + DX8[d];
        int ay = tileTY + DY8[d];
        int ddx = ax - sx, ddy = ay - sy;
        adjDist[d] = ddx * ddx + ddy * ddy;
        // Penalize occupied tiles so unoccupied ones are preferred
        if (tile_has_unit(ax, ay)) adjDist[d] += 10000;
    }
    // Simple insertion sort by distance
    for (int i = 1; i < 8; i++) {
        int key = adjOrder[i], kd = adjDist[i];
        int j = i - 1;
        while (j >= 0 && adjDist[j] > kd) {
            adjOrder[j + 1] = adjOrder[j];
            adjDist[j + 1] = adjDist[j];
            j--;
        }
        adjOrder[j + 1] = key;
        adjDist[j + 1] = kd;
    }
    for (int i = 0; i < 8; i++) {
        int d = adjOrder[i];
        int ax = tileTX + DX8[d];
        int ay = tileTY + DY8[d];
        if (terrain.passable(ax, ay)) {
            if (unit_find_path(sx, sy, ax, ay, terrain, u.pathDirs, u.pathLen, idx)) {
                unit_begin_path(u, sx, sy, ax, ay);
                u.state = USTATE_MOVING; // will switch to gathering on arrival
                pathed = true;
                break;
            }
        }
    }
    // If resource tile itself is passable (e.g., farm), go directly
    if (!pathed && terrain.passable(tileTX, tileTY)) {
        if (unit_find_path(sx, sy, tileTX, tileTY, terrain, u.pathDirs, u.pathLen, idx)) {
            unit_begin_path(u, sx, sy, tileTX, tileTY);
            u.state = USTATE_MOVING;
        }
    }
}

void unit_command_attack(int idx, int targetIdx) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    if (targetIdx < 0 || targetIdx >= MAX_UNITS || !units[targetIdx].alive) return;
    Unit& u = units[idx];
    u.cmdQueueLen = 0;
    u.patrolAX = -1;
    u.attackTarget = targetIdx;
    u.attackBldgTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;
    u.state = USTATE_ATTACKING;
}

void unit_command_attack_building(int idx, int bldgIdx) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    if (bldgIdx < 0 || bldgIdx >= MAX_BUILDINGS || !buildings[bldgIdx].alive) return;
    Unit& u = units[idx];
    u.cmdQueueLen = 0;
    u.patrolAX = -1;
    u.attackBldgTarget = bldgIdx;
    u.attackTarget = -1;
    u.buildTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;
}

void unit_command_build(int idx, int bldgIdx, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    if (bldgIdx < 0 || bldgIdx >= MAX_BUILDINGS || !buildings[bldgIdx].alive) return;
    Unit& u = units[idx];
    if (u.type != UNIT_VILLAGER) return;
    u.cmdQueueLen = 0;
    u.patrolAX = -1;
    u.buildTarget = bldgIdx;
    u.attackTarget = -1;
    u.attackBldgTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;
    u.role = VROLE_BUILDER;

    Building& b = buildings[bldgIdx];
    const BuildingStats& bst = BLDG_STATS[b.type];

    // Path to adjacent tile of building using two-pass approach:
    // first try unoccupied tiles so multiple builders don't stack,
    // then fall back to any passable tile if all are occupied.
    int bx = b.x / TILE_PX;
    int by = b.y / TILE_PX;
    int sx = u.x / TILE_PX;
    int sy = u.y / TILE_PX;

    // First pass: prefer unoccupied passable tiles
    for (int dy = -1; dy <= bst.tileH; dy++) {
        for (int dx = -1; dx <= bst.tileW; dx++) {
            if (dx >= 0 && dx < bst.tileW && dy >= 0 && dy < bst.tileH) continue;
            int tx = bx + dx;
            int ty = by + dy;
            if (terrain.passable(tx, ty) && !tile_has_unit(tx, ty)) {
                if (unit_find_path(sx, sy, tx, ty, terrain, u.pathDirs, u.pathLen, idx)) {
                    unit_begin_path(u, sx, sy, tx, ty);
                    u.state = USTATE_MOVING;
                    return;
                }
            }
        }
    }
    // Second pass: any passable tile
    for (int dy = -1; dy <= bst.tileH; dy++) {
        for (int dx = -1; dx <= bst.tileW; dx++) {
            if (dx >= 0 && dx < bst.tileW && dy >= 0 && dy < bst.tileH) continue;
            int tx = bx + dx;
            int ty = by + dy;
            if (terrain.passable(tx, ty)) {
                if (unit_find_path(sx, sy, tx, ty, terrain, u.pathDirs, u.pathLen, idx)) {
                    unit_begin_path(u, sx, sy, tx, ty);
                    u.state = USTATE_MOVING;
                    return;
                }
            }
        }
    }
}

void unit_command_garrison(int idx, int bldgIdx, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    if (bldgIdx < 0 || bldgIdx >= MAX_BUILDINGS || !buildings[bldgIdx].alive) return;
    units[idx].cmdQueueLen = 0;
    units[idx].patrolAX = -1;
    Building& b = buildings[bldgIdx];
    if (!building_is_complete(bldgIdx)) return;
    if (b.type != BLDG_TOWN_CENTER) return;
    if (b.owner != units[idx].owner) return;

    Unit& u = units[idx];
    u.garrisonTarget = bldgIdx;
    u.attackTarget = -1;
    u.attackBldgTarget = -1;
    u.buildTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;

    // Check if already adjacent to TC — garrison immediately
    const BuildingStats& bst = BLDG_STATS[b.type];
    int ux = u.x / TILE_PX;
    int uy = u.y / TILE_PX;
    int bx = b.x / TILE_PX;
    int by = b.y / TILE_PX;
    bool adjacent = (ux >= bx - 1 && ux <= bx + bst.tileW &&
                     uy >= by - 1 && uy <= by + bst.tileH);
    if (adjacent) {
        building_garrison(bldgIdx, idx);
        u.garrisonTarget = -1;
        return;
    }

    // Path to adjacent tile of TC
    int sx = u.x / TILE_PX;
    int sy = u.y / TILE_PX;
    for (int dy = -1; dy <= bst.tileH; dy++) {
        for (int dx = -1; dx <= bst.tileW; dx++) {
            if (dx >= 0 && dx < bst.tileW && dy >= 0 && dy < bst.tileH) continue;
            int tx = bx + dx;
            int ty = by + dy;
            if (terrain.passable(tx, ty)) {
                if (unit_find_path(sx, sy, tx, ty, terrain, u.pathDirs, u.pathLen, idx)) {
                    unit_begin_path(u, sx, sy, tx, ty);
                    u.state = USTATE_MOVING;
                    return;
                }
            }
        }
    }
}

void unit_command_patrol(int idx, s16 px, s16 py, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    Unit& u = units[idx];
    u.cmdQueueLen = 0;
    u.patrolAX = u.x; u.patrolAY = u.y; // start from current position
    u.patrolBX = px;  u.patrolBY = py;   // patrol to target
    u.attackTarget = -1;
    u.attackBldgTarget = -1;
    u.buildTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;
    u.garrisonTarget = -1;
    // Start moving to patrol point B
    unit_command_move(idx, px, py, terrain);
    // Restore patrol fields (command_move clears them via cmdQueueLen)
    u.patrolAX = u.x; u.patrolAY = u.y;
    u.patrolBX = px;  u.patrolBY = py;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------
int unit_at_pixel(s16 px, s16 py, int ignoreOwner) {
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
        if (ignoreOwner >= 0 && units[i].owner == (u8)ignoreOwner) continue;
        int dx = px - units[i].x;
        int dy = py - units[i].y;
        if (dx >= 0 && dx < TILE_PX && dy >= 0 && dy < TILE_PX) return i;
    }
    return -1;
}

int unit_count(int owner) {
    int c = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (units[i].alive && units[i].state != USTATE_DEAD &&
            units[i].owner == (u8)owner && units[i].type != UNIT_SHEEP) c++;
    }
    return c;
}

int unit_count_type(int owner, int type) {
    int c = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (units[i].alive && units[i].state != USTATE_DEAD &&
            units[i].owner == (u8)owner && units[i].type == (u8)type) c++;
    }
    return c;
}

int unit_find_idle_villager(int owner, int startFrom) {
    for (int i = 0; i < MAX_UNITS; i++) {
        int idx = (startFrom + i) % MAX_UNITS;
        if (units[idx].alive && units[idx].owner == (u8)owner &&
            units[idx].type == UNIT_VILLAGER && units[idx].state == USTATE_IDLE) {
            return idx;
        }
    }
    return -1;
}

int unit_find_nearest_enemy(int unitIdx) {
    if (unitIdx < 0 || !units[unitIdx].alive) return -1;
    Unit& u = units[unitIdx];
    int losPx = playerUnitStats[u.owner][u.type].los * TILE_PX;
    int bestDist = losPx * losPx + 1;
    int bestIdx = -1;

    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD || units[i].state == USTATE_GARRISONED) continue;
        if (units[i].owner == u.owner) continue;
        if (units[i].type == UNIT_SHEEP) continue; // don't target sheep as enemies
        int dx = units[i].x - u.x;
        int dy = units[i].y - u.y;
        int dist = dx * dx + dy * dy;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

// ---------------------------------------------------------------------------
// Per-frame update for all units
// ---------------------------------------------------------------------------
static void unit_update_gathering(Unit& u, GameState& gs, TerrainMap& terrain) {
    // Already at resource? Gather.
    int ux = (u.x + TILE_PX/2) / TILE_PX;
    int uy = (u.y + TILE_PX/2) / TILE_PX;

    // Check if adjacent to gather target
    int dx = ux - u.gatherTX; if (dx < 0) dx = -dx;
    int dy = uy - u.gatherTY; if (dy < 0) dy = -dy;
    bool adjacent = (dx <= 1 && dy <= 1);

    if (!adjacent) {
        // Need to path to resource
        u.state = USTATE_IDLE;
        unit_command_gather(&u - units, u.gatherTX, u.gatherTY, terrain);
        return;
    }

    // Face the resource
    int rdx = u.gatherTX * TILE_PX - u.x;
    int rdy = u.gatherTY * TILE_PX - u.y;
    u.direction = dir_from_delta(rdx, rdy);

    // Check if resource still exists
    u8 tt = terrain.tileAt(u.gatherTX, u.gatherTY);
    if (tt != TERRAIN_FOREST && tt != TERRAIN_GOLD && tt != TERRAIN_STONE &&
        tt != TERRAIN_FARM && tt != TERRAIN_BERRIES) {
        // Resource depleted — try to find nearest similar resource
        int newTX, newTY;
        if (u.carryType < RES_COUNT && find_nearest_resource(ux, uy, u.carryType, terrain, newTX, newTY)) {
            unit_command_gather(&u - units, newTX, newTY, terrain);
        } else {
            // No more resources of this type — return what we have
            if (u.carryAmount > 0) {
                u.state = USTATE_RETURNING;
            } else {
                u.state = USTATE_IDLE;
                u.gatherTX = -1;
                u.gatherTY = -1;
                u.role = VROLE_BASE;
            }
        }
        return;
    }

    // Gather tick (uses separate timer to avoid conflict with animation tick)
    u.gatherTick++;
    u8 rate = (u.carryType < RES_COUNT) ? playerGatherRate[u.owner][u.carryType] : GATHER_RATE;
    if (u.gatherTick >= rate) {
        u.gatherTick = 0;
        int got = terrain.depleteResource(u.gatherTX, u.gatherTY, 1);
        u.carryAmount += got;
        if (u.owner == 0 && tt == TERRAIN_FOREST) sound_play(SFX_CHOP);
    }

    // If carry full, return to drop-off
    if (u.carryAmount >= playerCarryMax[u.owner]) {
        u.state = USTATE_RETURNING;
    }
}

static void unit_update_returning(Unit& u, GameState& gs, TerrainMap& terrain) {
    // Find nearest drop-off building
    int bestBldg = building_nearest_dropoff(u.owner, u.carryType, u.x, u.y);
    if (bestBldg < 0) {
        // No drop-off available, idle
        u.state = USTATE_IDLE;
        return;
    }

    Building& b = buildings[bestBldg];

    // Check if adjacent to building
    int ux = (u.x + TILE_PX/2) / TILE_PX;
    int uy = (u.y + TILE_PX/2) / TILE_PX;
    int bx = b.x / TILE_PX;
    int by = b.y / TILE_PX;
    int bw = BLDG_STATS[b.type].tileW;
    int bh = BLDG_STATS[b.type].tileH;

    bool adjacent = (ux >= bx - 1 && ux <= bx + bw && uy >= by - 1 && uy <= by + bh);

    if (adjacent) {
        // Drop off resources
        if (u.carryType < RES_COUNT && u.carryAmount > 0) {
            game_add_resource(gs, u.owner, u.carryType, u.carryAmount);
            u.carryAmount = 0;
        }
        // Go back to gather
        if (u.gatherTX >= 0 && u.gatherTY >= 0) {
            u8 savedCarryType = u.carryType;
            unit_command_gather(&u - units, u.gatherTX, u.gatherTY, terrain);
            // If tile was depleted (command did nothing), find next resource
            if (u.state == USTATE_RETURNING) {
                int newTX, newTY;
                if (savedCarryType < RES_COUNT &&
                    find_nearest_resource(ux, uy, savedCarryType, terrain, newTX, newTY)) {
                    unit_command_gather(&u - units, newTX, newTY, terrain);
                }
                // If still stuck, go idle
                if (u.state == USTATE_RETURNING) {
                    u.state = USTATE_IDLE;
                    u.gatherTX = -1;
                    u.gatherTY = -1;
                    u.role = VROLE_BASE;
                }
            }
        } else {
            u.state = USTATE_IDLE;
        }
    } else {
        // Path to nearest adjacent passable tile of the building
        int sx = u.x / TILE_PX;
        int sy = u.y / TILE_PX;
        int bestNx = -1, bestNy = -1, bestDist = 99999;
        for (int dy = -1; dy <= bh; dy++) {
            for (int dx = -1; dx <= bw; dx++) {
                if (dx >= 0 && dx < bw && dy >= 0 && dy < bh) continue; // skip building tiles
                int nx = bx + dx;
                int ny = by + dy;
                if (!terrain.passable(nx, ny)) continue;
                int dist = (nx - sx) * (nx - sx) + (ny - sy) * (ny - sy);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestNx = nx;
                    bestNy = ny;
                }
            }
        }
        if (bestNx >= 0 && unit_find_path(sx, sy, bestNx, bestNy, terrain, u.pathDirs, u.pathLen, (int)(&u - units))) {
            unit_begin_path(u, sx, sy, bestNx, bestNy);
            u.state = USTATE_MOVING;
            // Will re-enter RETURNING when path completes
            return;
        }
        u.state = USTATE_IDLE;
    }
}

// ---------------------------------------------------------------------------
// Building construction state update
// ---------------------------------------------------------------------------
static void unit_update_building(Unit& u, GameState& gs, TerrainMap& terrain) {
    if (u.buildTarget < 0 || u.buildTarget >= MAX_BUILDINGS || !buildings[u.buildTarget].alive) {
        u.buildTarget = -1;
        u.state = USTATE_IDLE;
        u.role = VROLE_BASE;
        return;
    }

    Building& b = buildings[u.buildTarget];
    const BuildingStats& bst = BLDG_STATS[b.type];

    // Check if already complete
    if (b.buildProgress >= bst.buildTime) {
        // If building is damaged, repair it (costs wood)
        if (b.hp < bst.hp) {
            // Check if adjacent
            int ux = (u.x + TILE_PX/2) / TILE_PX;
            int uy = (u.y + TILE_PX/2) / TILE_PX;
            int bx2 = b.x / TILE_PX;
            int by2 = b.y / TILE_PX;
            bool adj = (ux >= bx2 - 1 && ux <= bx2 + bst.tileW &&
                        uy >= by2 - 1 && uy <= by2 + bst.tileH);
            if (!adj) {
                unit_command_build(&u - units, u.buildTarget, terrain);
                return;
            }
            // Face the building
            int bcx = b.x + (bst.tileW * TILE_PX) / 2;
            int bcy = b.y + (bst.tileH * TILE_PX) / 2;
            u.direction = dir_from_delta(bcx - u.x, bcy - u.y);
            u.role = VROLE_BUILDER;
            // Repair: restore 1 HP every 2 frames, cost 1 wood per 5 HP
            if ((gs.frameCount & 1) == 0) {
                if (gs.players[u.owner].resources[RES_WOOD] > 0) {
                    b.hp++;
                    if (b.hp > bst.hp) b.hp = bst.hp;
                    if ((b.hp % 5) == 0) {
                        gs.players[u.owner].resources[RES_WOOD]--;
                    }
                }
            }
            return;
        }
        u.buildTarget = -1;
        u.state = USTATE_IDLE;
        // If this was a farm, become farmer automatically
        if (b.type == BLDG_FARM) {
            int tx = b.x / TILE_PX;
            int ty = b.y / TILE_PX;
            unit_command_gather(&u - units, tx, ty, terrain);
        } else {
            u.role = VROLE_BASE;
        }
        return;
    }

    // Check if adjacent to building
    int ux = (u.x + TILE_PX/2) / TILE_PX;
    int uy = (u.y + TILE_PX/2) / TILE_PX;
    int bx = b.x / TILE_PX;
    int by = b.y / TILE_PX;

    bool adjacent = (ux >= bx - 1 && ux <= bx + bst.tileW && uy >= by - 1 && uy <= by + bst.tileH);

    if (!adjacent) {
        // Path to building
        unit_command_build(&u - units, u.buildTarget, terrain);
        return;
    }

    // Face the building
    int bCenterX = b.x + (bst.tileW * TILE_PX) / 2;
    int bCenterY = b.y + (bst.tileH * TILE_PX) / 2;
    int rdx = bCenterX - u.x;
    int rdy = bCenterY - u.y;
    u.direction = dir_from_delta(rdx, rdy);

    // Advance build progress (1 point per frame per villager)
    b.buildProgress++;
    if (b.buildProgress == bst.buildTime && u.owner == 0) {
        sound_play(SFX_BUILDING_COMPLETE);
    }
}

static void unit_update_attacking(Unit& u, GameState& gs, TerrainMap& terrain) {
    // --- Attacking a building ---
    if (u.attackBldgTarget >= 0) {
        if (u.attackBldgTarget >= MAX_BUILDINGS || !buildings[u.attackBldgTarget].alive) {
            u.attackBldgTarget = -1;
            u.state = USTATE_IDLE;
            return;
        }

        Building& bt = buildings[u.attackBldgTarget];
        const BuildingStats& bst = BLDG_STATS[bt.type];
        int bCenterX = bt.x + (bst.tileW * TILE_PX) / 2;
        int bCenterY = bt.y + (bst.tileH * TILE_PX) / 2;
        int dx = bCenterX - u.x;
        int dy = bCenterY - u.y;
        int dist2 = dx * dx + dy * dy;
        int rangePx = playerUnitStats[u.owner][u.type].range * TILE_PX + TILE_PX; // extra reach for buildings

        if (dist2 > rangePx * rangePx) {
            // Move toward building (save target — unit_command_move clears it)
            s8 savedBldg = u.attackBldgTarget;
            int idx = &u - units;
            unit_command_move(idx, bt.x, bt.y, terrain);
            u.attackBldgTarget = savedBldg;
            u.state = USTATE_MOVING;
            return;
        }

        // Face building
        u.direction = dir_from_delta(dx, dy);

        if (u.attackCooldown > 0) { u.attackCooldown--; return; }

        // Damage building (buildings have no armor classes)
        const s16* atk = playerUnitStats[u.owner][u.type].attack;
        int dmg = atk[DMG_MELEE] + atk[DMG_PIERCE]; // sum base damage classes
        // Siege units deal massive bonus damage to buildings
        if (u.type == UNIT_RAM) dmg += 125;
        else if (u.type == UNIT_MANGONEL) dmg += 35;
        if (dmg < 1) dmg = 1;

        building_damage(u.attackBldgTarget, dmg);
        u.attackCooldown = 30;
        if (u.owner == 0) sound_play(SFX_SWORD_HIT);

        if (bt.hp <= 0) {
            building_destroy(u.attackBldgTarget, terrain);
            u.attackBldgTarget = -1;
            u.state = USTATE_IDLE;
        }
        return;
    }

    // --- Attacking a unit ---
    if (u.attackTarget < 0 || u.attackTarget >= MAX_UNITS) {
        u.state = USTATE_IDLE;
        u.attackTarget = -1;
        return;
    }

    Unit& target = units[u.attackTarget];
    if (!target.alive || target.state == USTATE_DEAD) {
        u.attackTarget = -1;
        // Target already dead — retarget immediately
        int selfIdx = &u - units;
        int nextEnemy = unit_find_nearest_enemy(selfIdx);
        if (nextEnemy >= 0) {
            u.attackTarget = nextEnemy;
        } else {
            u.state = USTATE_IDLE;
        }
        return;
    }

    int dx = target.x - u.x;
    int dy = target.y - u.y;
    int dist2 = dx * dx + dy * dy;
    int rangePx = playerUnitStats[u.owner][u.type].range * TILE_PX;

    if (dist2 > rangePx * rangePx) {
        // Stand ground: don't chase, go idle
        if (u.stance == STANCE_STAND) {
            u.attackTarget = -1;
            u.state = USTATE_IDLE;
            return;
        }
        // Move toward target (save target — unit_command_move clears it)
        s8 savedTarget = u.attackTarget;
        int idx = &u - units;
        unit_command_move(idx, target.x, target.y, terrain);
        u.attackTarget = savedTarget;
        u.state = USTATE_MOVING;
        return;
    }

    // Face target
    u.direction = dir_from_delta(dx, dy);

    // Attack cooldown
    if (u.attackCooldown > 0) {
        u.attackCooldown--;
        return;
    }

    // Deal damage using class-based system
    const s16* atkClass = playerUnitStats[u.owner][u.type].attack;
    const s16* defClass = playerUnitStats[target.owner][target.type].armor;

    // Villager gathering from sheep: kill sheep and gain food
    if (u.type == UNIT_VILLAGER && target.type == UNIT_SHEEP) {
        unit_kill(u.attackTarget);
        u.attackTarget = -1;
        u.carryType = RES_FOOD;
        u.carryAmount = SHEEP_FOOD_AMOUNT;
        u.role = VROLE_FARMER;
        u.state = USTATE_RETURNING;
        return;
    }

    int dmg = calc_damage(atkClass, defClass, target.type);

    u.attackCooldown = 30; // ~0.5s between attacks

    // Ranged units fire projectiles instead of dealing instant damage
    if (u.type == UNIT_ARCHER) {
        projectile_spawn(u.x, u.y, target.x, target.y,
                         10, u.attackTarget, dmg, u.owner);
        if (u.owner == 0) sound_play(SFX_ARROW_FIRE);
        return;
    }
    if (u.type == UNIT_MANGONEL) {
        projectile_spawn(u.x, u.y, target.x, target.y,
                         20, u.attackTarget, dmg, u.owner);
        u.attackCooldown = 60; // mangonels fire slower
        return;
    }

    // Melee: instant damage
    target.hp -= dmg;

    // Trigger "under attack" alert for player 0
    if (target.owner == 0 && gs.underAttackTimer == 0) {
        gs.underAttackTimer = 180; // 3 seconds
        gs.attackAlertTX = target.x / TILE_PX;
        gs.attackAlertTY = target.y / TILE_PX;
    }

    if (u.owner == 0) sound_play(SFX_SWORD_HIT);

    if (target.hp <= 0) {
        unit_kill(u.attackTarget);
        u.attackTarget = -1;
        // Immediately search for next enemy instead of going idle for a frame
        int selfIdx = &u - units;
        int nextEnemy = unit_find_nearest_enemy(selfIdx);
        if (nextEnemy >= 0) {
            u.attackTarget = nextEnemy;
            u.state = USTATE_ATTACKING;
        } else {
            u.state = USTATE_IDLE;
        }
    }
}

// ---------------------------------------------------------------------------
// Command queue helpers
// ---------------------------------------------------------------------------
void unit_queue_command(int idx, Unit::CmdType type, s16 x, s16 y, s8 target) {
    Unit& u = units[idx];
    if (u.cmdQueueLen >= Unit::CMD_QUEUE_MAX) return;
    Unit::QueuedCmd& cmd = u.cmdQueue[u.cmdQueueLen++];
    cmd.type = type;
    cmd.x = x;
    cmd.y = y;
    cmd.target = target;
}

// Pop the front command from queue (shift remaining forward)
static bool unit_pop_command(Unit& u, Unit::QueuedCmd& out) {
    if (u.cmdQueueLen == 0) return false;
    out = u.cmdQueue[0];
    u.cmdQueueLen--;
    for (int i = 0; i < u.cmdQueueLen; i++)
        u.cmdQueue[i] = u.cmdQueue[i + 1];
    return true;
}

// Execute next queued command. Returns true if a command was dispatched.
static bool unit_exec_next_command(int idx, TerrainMap& terrain) {
    Unit& u = units[idx];
    Unit::QueuedCmd cmd;
    if (!unit_pop_command(u, cmd)) return false;

    switch (cmd.type) {
    case Unit::CMD_MOVE:
        unit_command_move(idx, cmd.x, cmd.y, terrain);
        break;
    case Unit::CMD_ATTACK:
        if (cmd.target >= 0 && cmd.target < MAX_UNITS && units[cmd.target].alive)
            unit_command_attack(idx, cmd.target);
        else return false;
        break;
    case Unit::CMD_ATTACK_BLDG:
        unit_command_attack_building(idx, cmd.target);
        break;
    case Unit::CMD_GATHER:
        unit_command_gather(idx, cmd.x, cmd.y, terrain);
        break;
    case Unit::CMD_BUILD:
        unit_command_build(idx, cmd.target, terrain);
        break;
    default:
        return false;
    }
    return true;
}

void units_update(GameState& gs, TerrainMap& terrain) {
    rebuild_occupancy();

    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        if (!u.alive) continue;

        // Skip garrisoned units — they're inside a building
        if (u.state == USTATE_GARRISONED) continue;

        if (u.state == USTATE_DEAD) {
            if (u.deadTimer > 0) {
                u.deadTimer--;
                // Advance death animation (hold last frame)
                u.animTick++;
                if (u.animTick >= 3) {
                    u.animTick = 0;
                    if (u.animFrame < 9) u.animFrame++;
                }
            } else {
                u.alive = false;
                if (u.spriteGfx) {
                    oamFreeGfx(&oamSub, u.spriteGfx);
                    u.spriteGfx = NULL;
                }
                u.oamSlot = -1;
                // Clear stale selection
                if (gs.unitSelected[i]) {
                    gs.unitSelected[i] = false;
                    gs.selectionCount--;
                    if (gs.selectedUnit == i) {
                        gs.selectedUnit = -1;
                        // Find next selected unit as primary
                        for (int j = 0; j < MAX_UNITS; j++) {
                            if (gs.unitSelected[j]) { gs.selectedUnit = j; break; }
                        }
                    }
                }
            }
            continue;
        }

        // Animation tick
        // 10 anim frames per cycle, advancing every tick (60fps) for movement
        // and every 2 ticks (30fps) for combat/work actions
        u.animTick++;
        int animSpeed = (u.state == USTATE_MOVING || u.state == USTATE_RETURNING ||
                         u.state == USTATE_SCOUTING) ? 1 :
                        (u.state == USTATE_IDLE) ? 4 : 2;
        if (u.animTick >= animSpeed) {
            u.animTick = 0;
            if (u.state == USTATE_MOVING || u.state == USTATE_ATTACKING ||
                u.state == USTATE_GATHERING || u.state == USTATE_RETURNING ||
                u.state == USTATE_BUILDING || u.state == USTATE_SCOUTING) {
                u.animFrame = (u.animFrame + 1) % 10;
            } else if (u.state == USTATE_IDLE) {
                u.animFrame = (u.animFrame + 1) % 5;
            } else {
                u.animFrame = 0;
            }
        }

        switch (u.state) {
        case USTATE_IDLE:
            // Patrol: swap waypoints and move to the other end
            if (u.patrolAX >= 0) {
                s16 pAX = u.patrolAX, pAY = u.patrolAY;
                s16 pBX = u.patrolBX, pBY = u.patrolBY;
                // Swap: next leg goes to B (current A becomes new B)
                unit_command_move(i, pBX, pBY, terrain);
                // Restore patrol (unit_command_move clears it)
                u.patrolAX = pBX; u.patrolAY = pBY;
                u.patrolBX = pAX; u.patrolBY = pAY;
                break;
            }
            // Process command queue before auto-engage
            if (u.cmdQueueLen > 0) {
                if (unit_exec_next_command(i, terrain))
                    break;
            }
            // Military units: auto-attack nearby enemies (stance-dependent)
            if (u.type != UNIT_VILLAGER && u.stance != STANCE_NO_ATTACK) {
                if (u.stance != STANCE_STAND) {
                    // Aggressive/Defensive: search for enemies in LOS
                    int enemy = unit_find_nearest_enemy(i);
                    if (enemy >= 0) {
                        u.attackTarget = enemy;
                        u.state = USTATE_ATTACKING;
                    }
                }
                // Stand ground: only attack if enemy is within weapon range
                if (u.stance == STANCE_STAND) {
                    int rangePx = playerUnitStats[u.owner][u.type].range * TILE_PX;
                    for (int e = 0; e < MAX_UNITS; e++) {
                        if (!units[e].alive || units[e].owner == u.owner) continue;
                        if (units[e].state == USTATE_DEAD) continue;
                        if (units[e].type == UNIT_SHEEP) continue;
                        int edx = units[e].x - u.x;
                        int edy = units[e].y - u.y;
                        if (edx * edx + edy * edy <= rangePx * rangePx) {
                            u.attackTarget = e;
                            u.state = USTATE_ATTACKING;
                            break;
                        }
                    }
                }
            }
            // Villagers: auto-flee from nearby enemy military (3 tile range)
            if (u.type == UNIT_VILLAGER && u.owner == 0) {
                int fleeDist = 3 * TILE_PX;
                for (int e = 0; e < MAX_UNITS; e++) {
                    if (!units[e].alive || units[e].owner == u.owner) continue;
                    if (units[e].state == USTATE_DEAD || units[e].state == USTATE_GARRISONED) continue;
                    if (units[e].type == UNIT_VILLAGER) continue; // don't flee from other villagers
                    int edx = units[e].x - u.x;
                    int edy = units[e].y - u.y;
                    if (edx * edx + edy * edy < fleeDist * fleeDist) {
                        // Flee to nearest TC
                        int tc = building_nearest(u.owner, BLDG_TOWN_CENTER, u.x, u.y);
                        if (tc >= 0) {
                            unit_command_move(i, buildings[tc].x, buildings[tc].y, terrain);
                        }
                        break;
                    }
                }
            }
            break;

        case USTATE_MOVING:
            unit_step_path(u, i, terrain);
            // Military units auto-engage enemies while moving (aggressive or patrol)
            if (u.state == USTATE_MOVING && u.type != UNIT_VILLAGER &&
                (u.stance == STANCE_AGGRESSIVE || u.patrolAX >= 0) &&
                u.attackTarget < 0 && u.attackBldgTarget < 0) {
                int enemy = unit_find_nearest_enemy(i);
                if (enemy >= 0) {
                    u.attackTarget = enemy;
                    u.state = USTATE_ATTACKING;
                }
            }
            // If path done, check what we should do next
            if (u.state == USTATE_IDLE) {
                if (u.garrisonTarget >= 0) {
                    // Arrived near TC — garrison
                    if (building_garrison(u.garrisonTarget, i)) {
                        u.garrisonTarget = -1;
                    } else {
                        // Garrison failed (full?), clear target
                        u.garrisonTarget = -1;
                    }
                } else if (u.buildTarget >= 0 && u.type == UNIT_VILLAGER) {
                    u.state = USTATE_BUILDING;
                } else if (u.gatherTX >= 0 && u.gatherTY >= 0 && u.type == UNIT_VILLAGER) {
                    u.state = USTATE_GATHERING;
                } else if (u.attackTarget >= 0 || u.attackBldgTarget >= 0) {
                    u.state = USTATE_ATTACKING;
                } else if (u.carryAmount > 0) {
                    u.state = USTATE_RETURNING;
                }
            }
            break;

        case USTATE_GATHERING:
            unit_update_gathering(u, gs, terrain);
            break;

        case USTATE_RETURNING:
            unit_update_returning(u, gs, terrain);
            break;

        case USTATE_ATTACKING:
            unit_update_attacking(u, gs, terrain);
            break;

        case USTATE_BUILDING:
            unit_update_building(u, gs, terrain);
            break;

        case USTATE_SCOUTING:
            // Auto-scout: move toward unexplored tiles
            // First check for nearby enemies and flee
            {
                int utx = (u.x + TILE_PX / 2) / TILE_PX;
                int uty = (u.y + TILE_PX / 2) / TILE_PX;

                // Check for enemies within 4 tiles
                int nearestEnemyDist = 99999;
                int enemyDX = 0, enemyDY = 0;
                for (int j = 0; j < MAX_UNITS; j++) {
                    if (!units[j].alive || units[j].state == USTATE_DEAD) continue;
                    if (units[j].owner == u.owner) continue;
                    int etx = units[j].x / TILE_PX;
                    int ety = units[j].y / TILE_PX;
                    int dx = etx - utx;
                    int dy = ety - uty;
                    int dist = dx * dx + dy * dy;
                    if (dist < nearestEnemyDist && dist <= 4 * 4) {
                        nearestEnemyDist = dist;
                        enemyDX = dx;
                        enemyDY = dy;
                    }
                }

                if (nearestEnemyDist <= 4 * 4) {
                    // Flee: move in opposite direction, 7 tiles away
                    int fleeDX = -enemyDX;
                    int fleeDY = -enemyDY;
                    // Normalize to 7 tiles
                    if (fleeDX == 0 && fleeDY == 0) { fleeDX = 1; fleeDY = 1; }
                    int fleeTX = utx + fleeDX * 7 / ((fleeDX < 0 ? -fleeDX : fleeDX) + (fleeDY < 0 ? -fleeDY : fleeDY) + 1);
                    int fleeTY = uty + fleeDY * 7 / ((fleeDX < 0 ? -fleeDX : fleeDX) + (fleeDY < 0 ? -fleeDY : fleeDY) + 1);
                    // Clamp to map
                    if (fleeTX < 1) fleeTX = 1;
                    if (fleeTY < 1) fleeTY = 1;
                    if (fleeTX >= MAP_TILES - 1) fleeTX = MAP_TILES - 2;
                    if (fleeTY >= MAP_TILES - 1) fleeTY = MAP_TILES - 2;
                    // Find passable tile near flee target
                    bool fled = false;
                    for (int r = 0; r <= 3 && !fled; r++) {
                        for (int dy = -r; dy <= r && !fled; dy++) {
                            for (int dx = -r; dx <= r && !fled; dx++) {
                                int ftx = fleeTX + dx;
                                int fty = fleeTY + dy;
                                if (ftx >= 0 && ftx < MAP_TILES && fty >= 0 && fty < MAP_TILES &&
                                    terrain.passable(ftx, fty)) {
                                    if (unit_find_path(utx, uty, ftx, fty, terrain, u.pathDirs, u.pathLen, i, true)) {
                                        unit_begin_path(u, utx, uty, ftx, fty);
                                        fled = true;
                                    }
                                }
                            }
                        }
                    }
                    break;
                }
            }

            // Only pick a new target when idle (reached previous one)
            if (u.pathLen == 0) {
                int utx = (u.x + TILE_PX / 2) / TILE_PX;
                int uty = (u.y + TILE_PX / 2) / TILE_PX;
                int bestDist = 999999;
                int bestTX = -1, bestTY = -1;
                // Sample every 2nd tile for performance
                // Require minimum distance of 5 tiles to avoid micro-oscillation
                int minDist = 5 * 5;
                for (int ty = 0; ty < MAP_TILES; ty += 2) {
                    for (int tx = 0; tx < MAP_TILES; tx += 2) {
                        if (fogMap.isExplored(u.owner, tx, ty)) continue;
                        if (!terrain.passable(tx, ty)) continue;
                        int dx = tx - utx;
                        int dy = ty - uty;
                        int dist = dx * dx + dy * dy;
                        if (dist >= minDist && dist < bestDist) {
                            bestDist = dist;
                            bestTX = tx;
                            bestTY = ty;
                        }
                    }
                }
                // Fallback: if no target at min distance, accept any
                if (bestTX < 0) {
                    bestDist = 999999;
                    for (int ty = 0; ty < MAP_TILES; ty += 2) {
                        for (int tx = 0; tx < MAP_TILES; tx += 2) {
                            if (fogMap.isExplored(u.owner, tx, ty)) continue;
                            if (!terrain.passable(tx, ty)) continue;
                            int dx = tx - utx;
                            int dy = ty - uty;
                            int dist = dx * dx + dy * dy;
                            if (dist < bestDist) {
                                bestDist = dist;
                                bestTX = tx;
                                bestTY = ty;
                            }
                        }
                    }
                }
                if (bestTX >= 0) {
                    u.targetX = bestTX * TILE_PX + TILE_PX / 2;
                    u.targetY = bestTY * TILE_PX + TILE_PX / 2;
                    int sx = u.x / TILE_PX;
                    int sy = u.y / TILE_PX;
                    if (!unit_find_path(sx, sy, bestTX, bestTY, terrain, u.pathDirs, u.pathLen, i, true)) {
                        // Path failed — mark nearby tiles as explored to avoid retrying
                        for (int dy2 = -2; dy2 <= 2; dy2++)
                            for (int dx2 = -2; dx2 <= 2; dx2++) {
                                int ex = bestTX + dx2, ey = bestTY + dy2;
                                if (ex >= 0 && ex < MAP_TILES && ey >= 0 && ey < MAP_TILES)
                                    fogMap.forceExplore(u.owner, ex, ey);
                            }
                    } else {
                        unit_begin_path(u, sx, sy, bestTX, bestTY);
                    }
                } else {
                    u.state = USTATE_IDLE; // map fully explored
                    break;
                }
            }
            // Move along path
            unit_step_path(u, i, terrain);
            // Stay in scouting state after reaching target
            if (u.state == USTATE_IDLE) {
                u.state = USTATE_SCOUTING;
            }
            break;
        }

        // Update pop count
        // (done in game_update via game_update_pop_cap)
    }

    // Recalculate pop counts
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gs.players[p].popCount = unit_count(p);
    }
}
