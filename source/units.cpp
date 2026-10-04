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

// Rebuild the occupancy grid from scratch. Only alive, visible, standing units
// are tracked (dead, garrisoned and moving units don't hold a tile).
static void rebuild_occupancy() {
    memset(tileOccupant, -1, sizeof(tileOccupant));
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD || units[i].state == USTATE_GARRISONED) continue;
        // Units on the move pass through each other; only standing ones hold a tile
        if (units[i].state == USTATE_MOVING || units[i].state == USTATE_SCOUTING) continue;
        int tx = (units[i].x + TILE_PX / 2) / TILE_PX;
        int ty = (units[i].y + TILE_PX / 2) / TILE_PX;
        if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES)
            tileOccupant[ty][tx] = i;
    }
}

// Record a unit placed mid-frame (spawn, ungarrison) so the next one placed
// in the same frame doesn't land on the same tile.
void tile_mark_unit(int tx, int ty, int unitIdx) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return;
    tileOccupant[ty][tx] = unitIdx;
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
        units[i].herdTarget = -1;
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
            u.herdTarget = -1;
            u.attackCooldown = 0;
            u.waitCounter = 0;
            u.stance = STANCE_AGGRESSIVE;
            u.deadTimer = 0;
            u.spriteGfx = NULL;
            u.oamSlot = -1;
            u.pathLen = 0;
            u.pathIdx = 0;
            u.subX = u.subY = 0;
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

// Forward declarations
static bool sheep_has_food(int idx, int owner);
static int find_nearest_sheep(const Unit& u);

// Direction offsets (8-directional), in Direction enum order N,NE,E,SE,S,SW,W,NW
static const s8 DX8[8] = { 0, 1, 1, 1, 0,-1,-1,-1};
static const s8 DY8[8] = {-1,-1, 0, 1, 1, 1, 0,-1};

// Nearest of the 8 directions to a vector in tile space. An axis counts once
// it is more than half the other (tan 22.5 degrees is about 0.41).
static Direction dir_from_delta(int dx, int dy) {
    int ax = (dx < 0) ? -dx : dx;
    int ay = (dy < 0) ? -dy : dy;
    int sx = (ax * 2 > ay) ? ((dx > 0) ? 1 : -1) : 0;
    int sy = (ay * 2 > ax) ? ((dy > 0) ? 1 : -1) : 0;
    for (int d = 0; d < 8; d++) {
        if (DX8[d] == sx && DY8[d] == sy) return (Direction)d;
    }
    return DIR_S; // zero vector
}

static int isqrt(int v) {
    if (v <= 0) return 0;
    int r = v, prev;
    do { prev = r; r = (r + v / r) / 2; } while (r < prev);
    return prev;
}

static int heuristic(int ax, int ay, int bx, int by) {
    int dx = ax - bx; if (dx < 0) dx = -dx;
    int dy = ay - by; if (dy < 0) dy = -dy;
    // Octile distance, matching the 10/14 step costs
    return (dx > dy) ? dx * 10 + dy * 4 : dy * 10 + dx * 4;
}

// Can a unit of this player stand on the tile? Terrain, buildings (own walls
// act as gates) — not other units, which come and go.
static bool tile_walkable(int tx, int ty, int owner, const TerrainMap& terrain) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return false;
    if (!terrain.passable(tx, ty)) return false;
    int b = building_at_tile(tx, ty);
    if (b < 0) return true;
    return buildings[b].type == BLDG_WALL && buildings[b].owner == owner;
}

// Build combined passability map for A* pathfinding.
// Layers: terrain -> buildings -> stationary units.
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

// Is every tile under a unit standing at pixel (px, py) passable? A unit
// covers one tile's worth of ground from its position.
static bool box_clear(int px, int py) {
    if (px < 0 || py < 0) return false;
    int tx0 = px / TILE_PX, ty0 = py / TILE_PX;
    int tx1 = (px + TILE_PX - 1) / TILE_PX, ty1 = (py + TILE_PX - 1) / TILE_PX;
    if (tx1 >= MAP_TILES || ty1 >= MAP_TILES) return false;
    return passMap[ty0][tx0] && passMap[ty0][tx1] && passMap[ty1][tx0] && passMap[ty1][tx1];
}

// Can a unit walk the straight line between two pixel positions? Sampled
// every quarter tile against passMap.
static bool line_clear(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0, dy = y1 - y0;
    int adx = (dx < 0) ? -dx : dx, ady = (dy < 0) ? -dy : dy;
    int n = ((adx > ady) ? adx : ady) / (TILE_PX / 4);
    for (int i = 1; i <= n; i++) {
        if (!box_clear(x0 + dx * i / n, y0 + dy * i / n)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Pathfinding
//
// A* over the tile grid, then the tile chain is pulled straight: a waypoint is
// kept only where the unit can no longer walk a straight line from the last
// one. Units then move between waypoints at any angle (unit_step_path), which
// is how the original moves — not tile to tile in eight directions.
//
// If the target tile is blocked (a building, a resource, a standing unit) the
// path ends on the nearest reachable tile beside it. With allowPartial the
// same happens when the target cannot be reached at all: the unit walks to
// the closest point it can get to instead of ignoring the order.
//
// On success the path is stored in the unit and pathDest is the tile it
// really ends on. Returns false, leaving the unit's path untouched, if there
// is nowhere better to go than where it stands and the target isn't adjacent.
// ---------------------------------------------------------------------------
bool unit_find_path(int sx, int sy, int tx, int ty, const TerrainMap& terrain,
                    Unit& u, int selfIdx, bool skipUnits, bool allowPartial) {
    // Clamp to map
    if (tx < 0) tx = 0;
    if (tx >= MAP_TILES) tx = MAP_TILES - 1;
    if (ty < 0) ty = 0;
    if (ty >= MAP_TILES) ty = MAP_TILES - 1;

    // Build combined passability map (terrain + buildings + optionally units);
    // the unit's own player's walls are passable (gate mechanic)
    build_pass_map(terrain, selfIdx, skipUnits, u.owner);

    // Ensure start tile is passable (unit might be on a building tile)
    passMap[sy][sx] = true;
    bool goalBlocked = !passMap[ty][tx];

    memset(astarGrid, 0, sizeof(astarGrid));
    for (int y = 0; y < MAP_TILES; y++)
        for (int x = 0; x < MAP_TILES; x++)
            astarGrid[y][x].parentX = astarGrid[y][x].parentY = -1;

    int openCount = 0;
    astarGrid[sy][sx].g = 0;
    astarGrid[sy][sx].f = heuristic(sx, sy, tx, ty);
    astarGrid[sy][sx].open = true;
    openList[openCount++] = sy * MAP_TILES + sx;

    int endX = -1, endY = -1;           // tile the path will end on
    int bestX = sx, bestY = sy;         // closest tile to the target seen so far
    int bestH = heuristic(sx, sy, tx, ty);

    while (openCount > 0) {
        // Open node with the lowest f
        int pick = 0;
        int pickF = astarGrid[openList[0] / MAP_TILES][openList[0] % MAP_TILES].f;
        for (int i = 1; i < openCount; i++) {
            int f = astarGrid[openList[i] / MAP_TILES][openList[i] % MAP_TILES].f;
            if (f < pickF) { pickF = f; pick = i; }
        }
        int cx = openList[pick] % MAP_TILES;
        int cy = openList[pick] / MAP_TILES;
        openList[pick] = openList[--openCount];
        astarGrid[cy][cx].open = false;
        astarGrid[cy][cx].closed = true;

        int h = heuristic(cx, cy, tx, ty);
        if (h < bestH) { bestH = h; bestX = cx; bestY = cy; }

        int ddx = cx - tx; if (ddx < 0) ddx = -ddx;
        int ddy = cy - ty; if (ddy < 0) ddy = -ddy;
        if ((ddx == 0 && ddy == 0) || (goalBlocked && ddx <= 1 && ddy <= 1)) {
            endX = cx; endY = cy;
            break;
        }

        for (int d = 0; d < 8; d++) {
            int nx = cx + DX8[d];
            int ny = cy + DY8[d];
            if (nx < 0 || nx >= MAP_TILES || ny < 0 || ny >= MAP_TILES) continue;
            if (astarGrid[ny][nx].closed) continue;
            if (!passMap[ny][nx]) continue;

            // Diagonal: don't cut through the corner of an obstacle
            if (DX8[d] != 0 && DY8[d] != 0) {
                if (!passMap[cy][cx + DX8[d]] || !passMap[cy + DY8[d]][cx]) continue;
            }

            int ng = astarGrid[cy][cx].g + ((DX8[d] != 0 && DY8[d] != 0) ? 14 : 10);
            if (!astarGrid[ny][nx].open || ng < astarGrid[ny][nx].g) {
                astarGrid[ny][nx].g = ng;
                astarGrid[ny][nx].f = ng + heuristic(nx, ny, tx, ty);
                astarGrid[ny][nx].parentX = cx;
                astarGrid[ny][nx].parentY = cy;
                if (!astarGrid[ny][nx].open) {
                    astarGrid[ny][nx].open = true;
                    openList[openCount++] = ny * MAP_TILES + nx;
                }
            }
        }
    }

    if (endX < 0) {
        // Target not reached: settle for the closest tile we could get to
        if (!goalBlocked && !allowPartial) return false;
        if (bestX == sx && bestY == sy) return false;
        endX = bestX; endY = bestY;
    }

    // Tile chain from the end back to the start (start tile excluded)
    static s16 chain[MAP_TILES * MAP_TILES];
    int chainLen = 0;
    for (int cx = endX, cy = endY; !(cx == sx && cy == sy); ) {
        chain[chainLen++] = cy * MAP_TILES + cx;
        int px = astarGrid[cy][cx].parentX;
        int py = astarGrid[cy][cx].parentY;
        cx = px; cy = py;
    }

    // Pull it straight, walking the chain from the unit's actual position.
    // If the path needs more waypoints than a unit holds, pathDest still
    // names the real end and unit_step_path paths again from the last one.
    u.pathLen = 0;
    u.pathIdx = 0;
    u.pathDestTX = endX;
    u.pathDestTY = endY;
    int ax = u.x, ay = u.y;
    for (int i = chainLen - 1; i >= 0 && u.pathLen < Unit::PATH_WP_MAX; i--) {
        bool last = (i == 0);
        if (!last) {
            // Keep going while the tile after this one is still in a straight line
            int nxt = chain[i - 1];
            if (line_clear(ax, ay, (nxt % MAP_TILES) * TILE_PX, (nxt / MAP_TILES) * TILE_PX)) continue;
        }
        u.wpX[u.pathLen] = chain[i] % MAP_TILES;
        u.wpY[u.pathLen] = chain[i] / MAP_TILES;
        u.pathLen++;
        ax = (chain[i] % MAP_TILES) * TILE_PX;
        ay = (chain[i] / MAP_TILES) * TILE_PX;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Nudge: push an idle friendly unit out of the way
//
// When a moving unit needs to step into a tile occupied by an idle friendly,
// the idle unit is given a 1-step path to the nearest empty adjacent tile.
// This prevents permanent blockages where idle units clog chokepoints.
// ---------------------------------------------------------------------------
static void nudge_unit(int idx, const TerrainMap& terrain) {
    Unit& other = units[idx];
    int ox = other.x / TILE_PX;
    int oy = other.y / TILE_PX;
    for (int d = 0; d < 8; d++) {
        int nx = ox + DX8[d];
        int ny = oy + DY8[d];
        if (!tile_walkable(nx, ny, other.owner, terrain)) continue;
        if (tileOccupant[ny][nx] >= 0) continue;
        other.wpX[0] = nx;
        other.wpY[0] = ny;
        other.pathLen = 1;
        other.pathIdx = 0;
        other.pathDestTX = nx;
        other.pathDestTY = ny;
        other.state = USTATE_MOVING;
        other.waitCounter = 0;
        tileOccupant[ny][nx] = idx;  // claimed, so two units aren't nudged onto it
        return;
    }
}

// Path again to the same destination from where the unit stands now; goes
// idle if there is no way.
static void unit_repath(Unit& u, int selfIdx, const TerrainMap& terrain) {
    int sx = (u.x + TILE_PX / 2) / TILE_PX;
    int sy = (u.y + TILE_PX / 2) / TILE_PX;
    if (!unit_find_path(sx, sy, u.pathDestTX, u.pathDestTY, terrain, u, selfIdx,
                        u.state == USTATE_SCOUTING, true)) {
        u.state = USTATE_IDLE;
        u.pathLen = 0;
    }
}

// ---------------------------------------------------------------------------
// Movement along the path
//
// The unit walks in a straight line toward its current waypoint at its own
// speed (UnitStats.speed, sixteenths of a pixel per frame), keeping the
// fraction in subX/subY so slow and diagonal movement stay exact.
//
// Entering a new tile is where it can be stopped:
//   - the tile has become unwalkable (a building went up): path again
//   - a friendly unit is standing there: an idle one is nudged aside, then we
//     wait, and after 8 frames path around it
//   - enemies never block (combat resolves the overlap), nor do moving units
// ---------------------------------------------------------------------------
static void unit_step_path(Unit& u, int selfIdx, const TerrainMap& terrain) {
    if (u.pathIdx >= u.pathLen) {
        // Out of waypoints. Either the path was longer than a unit can hold,
        // or we have arrived.
        int ctx = (u.x + TILE_PX / 2) / TILE_PX;
        int cty = (u.y + TILE_PX / 2) / TILE_PX;
        if (u.pathLen > 0 && (ctx != u.pathDestTX || cty != u.pathDestTY)) {
            unit_repath(u, selfIdx, terrain);
            if (u.state != USTATE_IDLE && u.pathLen > 0) return;
        }
        u.state = USTATE_IDLE;
        u.pathLen = 0;
        u.subX = u.subY = 0;
        u.waitCounter = 0;
        tile_mark_unit(ctx, cty, selfIdx);
        return;
    }

    // Speed in sixteenths of a pixel, slowed by the terrain underfoot
    int speed = playerUnitStats[u.owner][u.type].speed;
    int ctx = (u.x + TILE_PX / 2) / TILE_PX;
    int cty = (u.y + TILE_PX / 2) / TILE_PX;
    if (ctx >= 0 && ctx < MAP_TILES && cty >= 0 && cty < MAP_TILES) {
        speed = speed * TERRAIN_SPEED_MULT[terrain.tileAt(ctx, cty)] / 8;
        if (speed < 1) speed = 1;
    }

    int targetPX = u.wpX[u.pathIdx] * TILE_PX;
    int targetPY = u.wpY[u.pathIdx] * TILE_PX;
    int dx = targetPX - u.x;
    int dy = targetPY - u.y;
    int dist = isqrt(dx * dx + dy * dy);

    int nx, ny, nsubX = 0, nsubY = 0;
    bool reached = (dist * 16 <= speed);
    if (reached) {
        nx = targetPX;
        ny = targetPY;
    } else {
        // Step along the line, in 1/256 px, carrying the fraction
        int totX = u.subX + dx * speed * 16 / dist;
        int totY = u.subY + dy * speed * 16 / dist;
        nx = u.x + (totX >> 8);
        ny = u.y + (totY >> 8);
        nsubX = totX & 255;
        nsubY = totY & 255;
    }

    // Entering a new tile?
    int ntx = (nx + TILE_PX / 2) / TILE_PX;
    int nty = (ny + TILE_PX / 2) / TILE_PX;
    if ((ntx != ctx || nty != cty) && ntx >= 0 && ntx < MAP_TILES && nty >= 0 && nty < MAP_TILES) {
        if (!tile_walkable(ntx, nty, u.owner, terrain)) {
            unit_repath(u, selfIdx, terrain);
            return;
        }
        int occupant = tileOccupant[nty][ntx];
        if (occupant >= 0 && occupant != selfIdx && units[occupant].owner == u.owner &&
            u.state != USTATE_SCOUTING) {
            if (units[occupant].state == USTATE_IDLE) nudge_unit(occupant, terrain);
            u.waitCounter++;
            if (u.waitCounter >= 8) {
                u.waitCounter = 0;
                unit_repath(u, selfIdx, terrain);
            }
            return; // don't step this frame
        }
    }
    u.waitCounter = 0;

    u.x = nx;
    u.y = ny;
    u.subX = nsubX;
    u.subY = nsubY;
    if (reached) u.pathIdx++;

    // Clamp to map
    if (u.x < 0) u.x = 0;
    if (u.y < 0) u.y = 0;
    if (u.x > MAP_PX - TILE_PX) u.x = MAP_PX - TILE_PX;
    if (u.y > MAP_PX - TILE_PX) u.y = MAP_PX - TILE_PX;

    // Face the way we are walking
    if (dx != 0 || dy != 0) u.direction = dir_from_delta(dx, dy);
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
// Helper: path a unit up to a building. Aims at the building tile nearest the
// unit; that tile is blocked, so the path ends on the closest free tile beside
// it — one search, where trying each tile round the building took up to 40.
// ---------------------------------------------------------------------------
static bool path_to_building(Unit& u, int idx, const Building& b, const TerrainMap& terrain) {
    const BuildingStats& bst = BLDG_STATS[b.type];
    int sx = u.x / TILE_PX, sy = u.y / TILE_PX;
    int bx = b.x / TILE_PX, by = b.y / TILE_PX;
    int tx = (sx < bx) ? bx : (sx >= bx + bst.tileW) ? bx + bst.tileW - 1 : sx;
    int ty = (sy < by) ? by : (sy >= by + bst.tileH) ? by + bst.tileH - 1 : sy;
    return unit_find_path(sx, sy, tx, ty, terrain, u, idx);
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

    // A move order always goes somewhere: to the closest reachable point if
    // the spot itself can't be reached
    if (unit_find_path(sx, sy, tx, ty, terrain, u, idx, false, true)) {
        u.state = USTATE_MOVING;
        u.attackTarget = -1;
        u.attackBldgTarget = -1;
        u.buildTarget = -1;
        u.garrisonTarget = -1;
        u.herdTarget = -1;
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
    u.herdTarget = -1;
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
            if (unit_find_path(sx, sy, ax, ay, terrain, u, idx)) {
                u.state = USTATE_MOVING; // will switch to gathering on arrival
                pathed = true;
                break;
            }
        }
    }
    // If resource tile itself is passable (e.g., farm), go directly
    if (!pathed && terrain.passable(tileTX, tileTY)) {
        if (unit_find_path(sx, sy, tileTX, tileTY, terrain, u, idx)) {
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
    u.convertProgress = 0;
    u.state = USTATE_ATTACKING;
    // A villager sent at its own sheep keeps working it (see unit_update_shepherd)
    const Unit& t = units[targetIdx];
    u.herdTarget = (u.type == UNIT_VILLAGER && t.type == UNIT_SHEEP && t.owner == u.owner) ? targetIdx : -1;
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
    u.herdTarget = -1;
    u.role = VROLE_BUILDER;

    Building& b = buildings[bldgIdx];

    if (path_to_building(u, idx, b, terrain)) u.state = USTATE_MOVING;
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
    if (units[idx].type == UNIT_SHEEP) return;

    Unit& u = units[idx];
    u.herdTarget = -1;
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

    if (path_to_building(u, idx, b, terrain)) u.state = USTATE_MOVING;
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
        if (u.owner == 0) {
            if (tt == TERRAIN_FOREST) sound_play(SFX_CHOP);
            else if (tt == TERRAIN_GOLD || tt == TERRAIN_STONE) sound_play(SFX_MINE);
        }
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
        } else if (u.herdTarget >= 0) {
            // Back to the sheep we were working, or the next one
            if (!sheep_has_food(u.herdTarget, u.owner)) u.herdTarget = find_nearest_sheep(u);
            if (u.herdTarget >= 0) {
                u.attackTarget = u.herdTarget;
                u.state = USTATE_ATTACKING;
            } else {
                u.state = USTATE_IDLE;
                u.role = VROLE_BASE;
            }
        } else {
            u.state = USTATE_IDLE;
        }
    } else {
        // Walk up to the building; RETURNING resumes when the path completes
        if (path_to_building(u, &u - units, b, terrain)) {
            u.state = USTATE_MOVING;
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
        int self = &u - units;
        int btx = b.x / TILE_PX;
        int bty = b.y / TILE_PX;
        // Another unfinished building of ours close by? Help with that next.
        int next = -1, nextDist = (8 * TILE_PX) * (8 * TILE_PX);
        for (int i = 0; i < MAX_BUILDINGS; i++) {
            if (!buildings[i].alive || buildings[i].owner != u.owner || building_is_complete(i)) continue;
            int ddx = buildings[i].x - u.x, ddy = buildings[i].y - u.y;
            int dist = ddx * ddx + ddy * ddy;
            if (dist < nextDist) { nextDist = dist; next = i; }
        }
        int rtx, rty;
        if (next >= 0) {
            unit_command_build(self, next, terrain);
        } else if (b.type == BLDG_FARM) {
            // A farm's builder becomes its farmer
            unit_command_gather(self, btx, bty, terrain);
        } else if (b.type == BLDG_LUMBER_CAMP &&
                   find_nearest_resource(btx, bty, RES_WOOD, terrain, rtx, rty)) {
            unit_command_gather(self, rtx, rty, terrain);
        } else if (b.type == BLDG_MINING_CAMP &&
                   (find_nearest_resource(btx, bty, RES_GOLD, terrain, rtx, rty) ||
                    find_nearest_resource(btx, bty, RES_STONE, terrain, rtx, rty))) {
            unit_command_gather(self, rtx, rty, terrain);
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

    // Advance build progress (1 point per frame per villager). The foundation
    // starts at a tenth of its hit points and gains the rest as it goes up.
    int hpBase = bst.hp / 10;
    int hpBefore = hpBase + (bst.hp - hpBase) * b.buildProgress / bst.buildTime;
    b.buildProgress++;
    int hpAfter = hpBase + (bst.hp - hpBase) * b.buildProgress / bst.buildTime;
    b.hp += hpAfter - hpBefore;
    if (b.buildProgress == bst.buildTime && u.owner == 0) {
        sound_play(SFX_BUILDING_COMPLETE);
    }
}

// ---------------------------------------------------------------------------
// Villager working a sheep
//
// The sheep is killed on contact and becomes a carcass holding
// SHEEP_FOOD_AMOUNT food in its carryAmount. The villager carves it like any
// other resource: fill up, walk to the drop-off, come back (herdTarget
// remembers the carcass across the trip), and move on to the next sheep once
// it is picked clean.
// ---------------------------------------------------------------------------
static bool sheep_has_food(int idx, int owner) {
    if (idx < 0 || idx >= MAX_UNITS) return false;
    const Unit& s = units[idx];
    if (!s.alive || s.type != UNIT_SHEEP || s.owner != owner) return false;
    return s.state != USTATE_DEAD || s.carryAmount > 0;
}

static int find_nearest_sheep(const Unit& u) {
    int best = -1, bestDist = (12 * TILE_PX) * (12 * TILE_PX);
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!sheep_has_food(i, u.owner)) continue;
        int dx = units[i].x - u.x, dy = units[i].y - u.y;
        int dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = i; }
    }
    return best;
}

static void unit_update_shepherd(Unit& u, TerrainMap& terrain) {
    int self = &u - units;
    if (!sheep_has_food(u.attackTarget, u.owner)) {
        // Picked clean (or gone): drop off what we have, then the next sheep
        u.attackTarget = -1;
        u.herdTarget = find_nearest_sheep(u);
        if (u.carryAmount > 0) {
            u.state = USTATE_RETURNING;
        } else if (u.herdTarget >= 0) {
            u.attackTarget = u.herdTarget;
        } else {
            u.state = USTATE_IDLE;
            u.role = VROLE_BASE;
        }
        return;
    }

    Unit& sheep = units[u.attackTarget];
    int dtx = (sheep.x + TILE_PX / 2) / TILE_PX - (u.x + TILE_PX / 2) / TILE_PX;
    int dty = (sheep.y + TILE_PX / 2) / TILE_PX - (u.y + TILE_PX / 2) / TILE_PX;
    if (dtx < -1 || dtx > 1 || dty < -1 || dty > 1) {
        // Walk over (unit_command_move clears the targets, so put them back)
        s8 target = u.attackTarget;
        u.state = USTATE_IDLE;
        unit_command_move(self, sheep.x, sheep.y, terrain);
        if (u.state != USTATE_MOVING) {  // unreachable
            u.attackTarget = -1;
            u.herdTarget = -1;
            return;
        }
        u.attackTarget = target;
        u.herdTarget = target;
        return;
    }

    u.direction = dir_from_delta(sheep.x - u.x, sheep.y - u.y);
    u.role = VROLE_FARMER;
    if (u.carryType != RES_FOOD) {
        u.carryType = RES_FOOD;
        u.carryAmount = 0;
    }

    if (sheep.state != USTATE_DEAD) {
        sheep.state = USTATE_DEAD;
        sheep.deadTimer = 300;
        sheep.animFrame = 0;
        sheep.animTick = 0;
        sheep.carryAmount = SHEEP_FOOD_AMOUNT;
        return;
    }

    if (u.carryAmount >= playerCarryMax[u.owner]) {
        u.attackTarget = -1;  // herdTarget brings us back after the drop-off
        u.state = USTATE_RETURNING;
        return;
    }

    u.gatherTick++;
    if (u.gatherTick >= playerGatherRate[u.owner][RES_FOOD]) {
        u.gatherTick = 0;
        sheep.carryAmount--;
        u.carryAmount++;
    }
}

static void unit_update_attacking(Unit& u, GameState& gs, TerrainMap& terrain) {
    // --- Attacking a building ---
    if (u.attackBldgTarget >= 0) {
        // Monks can't attack buildings
        if (u.type == UNIT_MONK) {
            u.attackBldgTarget = -1;
            u.state = USTATE_IDLE;
            return;
        }
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
        // Reach is measured in tiles from the edge of the footprint, not from
        // its centre: a melee unit beside a 4x4 building is 3 tiles from the
        // centre and could never hit it.
        int utx = (u.x + TILE_PX / 2) / TILE_PX;
        int uty = (u.y + TILE_PX / 2) / TILE_PX;
        int bx0 = bt.x / TILE_PX, by0 = bt.y / TILE_PX;
        int gapX = (utx < bx0) ? bx0 - utx : (utx >= bx0 + bst.tileW) ? utx - (bx0 + bst.tileW - 1) : 0;
        int gapY = (uty < by0) ? by0 - uty : (uty >= by0 + bst.tileH) ? uty - (by0 + bst.tileH - 1) : 0;
        int gap = (gapX > gapY) ? gapX : gapY;

        if (gap > playerUnitStats[u.owner][u.type].range) {
            // Move toward building (save target — unit_command_move clears it)
            s8 savedBldg = u.attackBldgTarget;
            int idx = &u - units;
            u.state = USTATE_IDLE;
            unit_command_move(idx, bt.x, bt.y, terrain);
            if (u.state != USTATE_MOVING) {  // can't get there: give up
                u.attackBldgTarget = -1;
                return;
            }
            u.attackBldgTarget = savedBldg;
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
    if (u.type == UNIT_VILLAGER && u.herdTarget == u.attackTarget) {
        unit_update_shepherd(u, terrain);
        return;
    }
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
    // Melee reaches every adjacent tile; a diagonal neighbour is 22px away,
    // which a 16px circle misses, leaving the attacker shuffling forever.
    bool inRange = (rangePx <= TILE_PX)
        ? (dx >= -TILE_PX && dx <= TILE_PX && dy >= -TILE_PX && dy <= TILE_PX)
        : (dist2 <= rangePx * rangePx);

    if (!inRange) {
        // Stand ground: don't chase, go idle
        if (u.stance == STANCE_STAND) {
            u.attackTarget = -1;
            u.state = USTATE_IDLE;
            return;
        }
        // Move toward target (save target — unit_command_move clears it)
        s8 savedTarget = u.attackTarget;
        int idx = &u - units;
        u.state = USTATE_IDLE;
        unit_command_move(idx, target.x, target.y, terrain);
        if (u.state != USTATE_MOVING) {  // can't get there: give up
            u.attackTarget = -1;
            return;
        }
        u.attackTarget = savedTarget;
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

    // Monk conversion: gradually convert enemy unit to own side
    if (u.type == UNIT_MONK) {
        // Can't convert monks, siege units, or sheep
        if (target.type == UNIT_MONK || target.type == UNIT_RAM ||
            target.type == UNIT_MANGONEL || target.type == UNIT_SHEEP) {
            u.attackTarget = -1;
            u.convertProgress = 0;
            u.state = USTATE_IDLE;
            return;
        }
        u.convertProgress++;
        if (u.convertProgress >= 240) { // ~4 seconds at 60fps
            // Convert the target unit
            target.owner = u.owner;
            u.attackTarget = -1;
            u.convertProgress = 0;
            u.state = USTATE_IDLE;
        }
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
            // A sheep carcass stays while it has meat, which slowly rots
            if (u.type == UNIT_SHEEP && u.carryAmount > 0) {
                if (u.deadTimer < 60) u.deadTimer = 60;
                if (gs.frameCount % 120 == 0) u.carryAmount--;
            }
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
                                    if (unit_find_path(utx, uty, ftx, fty, terrain, u, i, true)) {
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
                    if (!unit_find_path(sx, sy, bestTX, bestTY, terrain, u, i, true)) {
                        // Path failed — mark nearby tiles as explored to avoid retrying
                        for (int dy2 = -2; dy2 <= 2; dy2++)
                            for (int dx2 = -2; dx2 <= 2; dx2++) {
                                int ex = bestTX + dx2, ey = bestTY + dy2;
                                if (ex >= 0 && ex < MAP_TILES && ey >= 0 && ey < MAP_TILES)
                                    fogMap.forceExplore(u.owner, ex, ey);
                            }
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
