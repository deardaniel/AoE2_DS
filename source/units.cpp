#include "units.h"
#include "game.h"
#include "buildings.h"
#include "terrain.h"
#include "tech.h"
#include <string.h>

Unit units[MAX_UNITS];

void units_init() {
    memset(units, 0, sizeof(units));
    for (int i = 0; i < MAX_UNITS; i++) {
        units[i].alive = false;
        units[i].attackTarget = -1;
        units[i].attackBldgTarget = -1;
        units[i].gatherTX = -1;
        units[i].gatherTY = -1;
        units[i].oamSlot = -1;
        units[i].carryType = RES_COUNT;
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
            u.direction = DIR_DOWN;
            u.animFrame = 0;
            u.animTick = 0;
            u.gatherTick = 0;
            u.carryType = RES_COUNT;
            u.carryAmount = 0;
            u.gatherTX = -1; u.gatherTY = -1;
            u.attackTarget = -1;
            u.attackBldgTarget = -1;
            u.attackCooldown = 0;
            u.deadTimer = 0;
            u.spriteGfx = NULL;
            u.oamSlot = -1;
            u.pathLen = 0;
            u.pathIdx = 0;
            return i;
        }
    }
    return -1;
}

void unit_kill(int idx) {
    if (idx < 0 || idx >= MAX_UNITS) return;
    Unit& u = units[idx];
    u.state = USTATE_DEAD;
    u.deadTimer = 30; // show death briefly
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
// Map 8-dir to 4-dir for animation
static const u8 DIR8_TO_4[8] = { DIR_UP, DIR_RIGHT, DIR_RIGHT, DIR_RIGHT, DIR_DOWN, DIR_LEFT, DIR_LEFT, DIR_LEFT };

static int heuristic(int ax, int ay, int bx, int by) {
    int dx = ax - bx; if (dx < 0) dx = -dx;
    int dy = ay - by; if (dy < 0) dy = -dy;
    return (dx > dy) ? dx : dy; // Chebyshev distance
}

// Build combined passability map: terrain + buildings
static void build_pass_map(const TerrainMap& terrain) {
    for (int y = 0; y < MAP_TILES; y++)
        for (int x = 0; x < MAP_TILES; x++)
            passMap[y][x] = terrain.passable(x, y);

    // Mark building tiles as impassable
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
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
}

bool unit_find_path(int sx, int sy, int tx, int ty, const TerrainMap& terrain,
                    u8* outDirs, u8& outLen) {
    outLen = 0;
    if (sx == tx && sy == ty) return true;

    // Clamp to map
    if (tx < 0) tx = 0;
    if (tx >= MAP_TILES) tx = MAP_TILES - 1;
    if (ty < 0) ty = 0;
    if (ty >= MAP_TILES) ty = MAP_TILES - 1;

    // Build combined passability map (terrain + buildings)
    build_pass_map(terrain);

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

// ---------------------------------------------------------------------------
// Movement along path
// Uses pre-computed step target (stepTX, stepTY) to avoid drift from
// recalculating the target tile each frame based on current position.
// ---------------------------------------------------------------------------
static void unit_step_path(Unit& u, const TerrainMap& terrain) {
    if (u.pathIdx >= u.pathLen) {
        // Snap to final destination and stop
        u.x = u.pathDestTX * TILE_PX;
        u.y = u.pathDestTY * TILE_PX;
        u.state = USTATE_IDLE;
        u.pathLen = 0;
        return;
    }

    int speed = playerUnitStats[u.owner][u.type].speed;
    int targetPX = u.stepTX * TILE_PX;
    int targetPY = u.stepTY * TILE_PX;

    int dx = targetPX - u.x;
    int dy = targetPY - u.y;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;

    if (adx <= speed && ady <= speed) {
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
    u.direction = DIR8_TO_4[u.pathDirs[dirIdx]];
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
// Commands
// ---------------------------------------------------------------------------
void unit_command_move(int idx, s16 px, s16 py, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    Unit& u = units[idx];

    int sx = u.x / TILE_PX;
    int sy = u.y / TILE_PX;
    int tx = px / TILE_PX;
    int ty = py / TILE_PX;

    if (unit_find_path(sx, sy, tx, ty, terrain, u.pathDirs, u.pathLen)) {
        unit_begin_path(u, sx, sy, tx, ty);
        u.state = USTATE_MOVING;
        u.attackTarget = -1;
        u.attackBldgTarget = -1;
        u.gatherTX = -1;
        u.gatherTY = -1;
    }
}

void unit_command_gather(int idx, int tileTX, int tileTY, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    Unit& u = units[idx];
    if (u.type != UNIT_VILLAGER) return;

    u8 tt = terrain.tileAt(tileTX, tileTY);
    if (tt != TERRAIN_FOREST && tt != TERRAIN_GOLD && tt != TERRAIN_STONE && tt != TERRAIN_FARM)
        return;

    u.gatherTX = tileTX;
    u.gatherTY = tileTY;
    u.attackTarget = -1;
    u.carryType = RES_COUNT;
    u.carryAmount = 0;

    // Determine carry type from terrain
    if (tt == TERRAIN_FOREST) u.carryType = RES_WOOD;
    else if (tt == TERRAIN_GOLD) u.carryType = RES_GOLD;
    else if (tt == TERRAIN_STONE) u.carryType = RES_STONE;
    else if (tt == TERRAIN_FARM) u.carryType = RES_FOOD;

    // Find path to adjacent passable tile
    int sx = u.x / TILE_PX;
    int sy = u.y / TILE_PX;

    // Try to path to an adjacent tile of the resource
    bool pathed = false;
    for (int d = 0; d < 8; d++) {
        int ax = tileTX + DX8[d];
        int ay = tileTY + DY8[d];
        if (terrain.passable(ax, ay)) {
            if (unit_find_path(sx, sy, ax, ay, terrain, u.pathDirs, u.pathLen)) {
                unit_begin_path(u, sx, sy, ax, ay);
                u.state = USTATE_MOVING; // will switch to gathering on arrival
                pathed = true;
                break;
            }
        }
    }
    // If resource tile itself is passable (e.g., farm), go directly
    if (!pathed && terrain.passable(tileTX, tileTY)) {
        if (unit_find_path(sx, sy, tileTX, tileTY, terrain, u.pathDirs, u.pathLen)) {
            unit_begin_path(u, sx, sy, tileTX, tileTY);
            u.state = USTATE_MOVING;
        }
    }
}

void unit_command_attack(int idx, int targetIdx) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    if (targetIdx < 0 || targetIdx >= MAX_UNITS || !units[targetIdx].alive) return;
    Unit& u = units[idx];
    u.attackTarget = targetIdx;
    u.attackBldgTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;
}

void unit_command_attack_building(int idx, int bldgIdx) {
    if (idx < 0 || idx >= MAX_UNITS || !units[idx].alive) return;
    if (bldgIdx < 0 || bldgIdx >= MAX_BUILDINGS || !buildings[bldgIdx].alive) return;
    Unit& u = units[idx];
    u.attackBldgTarget = bldgIdx;
    u.attackTarget = -1;
    u.gatherTX = -1;
    u.gatherTY = -1;
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
        if (units[i].alive && units[i].state != USTATE_DEAD && units[i].owner == (u8)owner) c++;
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
        if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
        if (units[i].owner == u.owner) continue;
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
    if (rdx > 0) u.direction = DIR_RIGHT;
    else if (rdx < 0) u.direction = DIR_LEFT;
    else if (rdy > 0) u.direction = DIR_DOWN;
    else u.direction = DIR_UP;

    // Check if resource still exists
    u8 tt = terrain.tileAt(u.gatherTX, u.gatherTY);
    if (tt != TERRAIN_FOREST && tt != TERRAIN_GOLD && tt != TERRAIN_STONE && tt != TERRAIN_FARM) {
        u.state = USTATE_IDLE;
        u.gatherTX = -1;
        u.gatherTY = -1;
        return;
    }

    // Gather tick (uses separate timer to avoid conflict with animation tick)
    u.gatherTick++;
    if (u.gatherTick >= GATHER_RATE) {
        u.gatherTick = 0;
        int got = terrain.depleteResource(u.gatherTX, u.gatherTY, 1);
        u.carryAmount += got;
    }

    // If carry full, return to drop-off
    if (u.carryAmount >= GATHER_CARRY_MAX) {
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
            unit_command_gather(&u - units, u.gatherTX, u.gatherTY, terrain);
        } else {
            u.state = USTATE_IDLE;
        }
    } else {
        // Path to building
        // Find adjacent passable tile
        for (int dy = -1; dy <= bh; dy++) {
            for (int dx = -1; dx <= bw; dx++) {
                if (dx >= 0 && dx < bw && dy >= 0 && dy < bh) continue; // skip building tiles
                int nx = bx + dx;
                int ny = by + dy;
                if (terrain.passable(nx, ny)) {
                    int sx = u.x / TILE_PX;
                    int sy = u.y / TILE_PX;
                    if (unit_find_path(sx, sy, nx, ny, terrain, u.pathDirs, u.pathLen)) {
                        unit_begin_path(u, sx, sy, nx, ny);
                        u.state = USTATE_MOVING;
                        // Will re-enter RETURNING when path completes
                        return;
                    }
                }
            }
        }
        u.state = USTATE_IDLE;
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
        if (dx > 0) u.direction = DIR_RIGHT;
        else if (dx < 0) u.direction = DIR_LEFT;
        else if (dy > 0) u.direction = DIR_DOWN;
        else u.direction = DIR_UP;

        if (u.attackCooldown > 0) { u.attackCooldown--; return; }

        // Damage building (no armor on buildings)
        int atk = playerUnitStats[u.owner][u.type].attack;
        int dmg = atk;
        if (dmg < 1) dmg = 1;

        building_damage(u.attackBldgTarget, dmg);
        u.attackCooldown = 30;

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
        u.state = USTATE_IDLE;
        u.attackTarget = -1;
        return;
    }

    int dx = target.x - u.x;
    int dy = target.y - u.y;
    int dist2 = dx * dx + dy * dy;
    int rangePx = playerUnitStats[u.owner][u.type].range * TILE_PX;

    if (dist2 > rangePx * rangePx) {
        // Move toward target (save target — unit_command_move clears it)
        s8 savedTarget = u.attackTarget;
        int idx = &u - units;
        unit_command_move(idx, target.x, target.y, terrain);
        u.attackTarget = savedTarget;
        u.state = USTATE_MOVING;
        return;
    }

    // Face target
    if (dx > 0) u.direction = DIR_RIGHT;
    else if (dx < 0) u.direction = DIR_LEFT;
    else if (dy > 0) u.direction = DIR_DOWN;
    else u.direction = DIR_UP;

    // Attack cooldown
    if (u.attackCooldown > 0) {
        u.attackCooldown--;
        return;
    }

    // Deal damage using tech-modified stats
    int atk = playerUnitStats[u.owner][u.type].attack;
    int arm = playerUnitStats[target.owner][target.type].armor;

    // Spearman bonus vs cavalry (+15 in real AoE2)
    if (u.type == UNIT_SPEARMAN && target.type == UNIT_KNIGHT) {
        atk += 15;
    }

    int dmg = atk - arm;
    if (dmg < 1) dmg = 1;

    target.hp -= dmg;
    u.attackCooldown = 30; // ~0.5s between attacks

    if (target.hp <= 0) {
        unit_kill(u.attackTarget);
        u.attackTarget = -1;
        u.state = USTATE_IDLE;
    }
}

void units_update(GameState& gs, TerrainMap& terrain) {
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        if (!u.alive) continue;

        if (u.state == USTATE_DEAD) {
            if (u.deadTimer > 0) {
                u.deadTimer--;
            } else {
                u.alive = false;
                if (u.spriteGfx) {
                    oamFreeGfx(&oamSub, u.spriteGfx);
                    u.spriteGfx = NULL;
                }
                u.oamSlot = -1;
                // Clear stale selection
                if (gs.selectedUnit == i) gs.selectedUnit = -1;
            }
            continue;
        }

        // Animation tick (5 frames = ~12fps walk, 8 frames = ~7.5fps attack)
        u.animTick++;
        int animSpeed = (u.state == USTATE_MOVING || u.state == USTATE_RETURNING) ? 5 : 8;
        if (u.animTick >= animSpeed) {
            u.animTick = 0;
            if (u.state == USTATE_MOVING || u.state == USTATE_ATTACKING ||
                u.state == USTATE_GATHERING || u.state == USTATE_RETURNING) {
                u.animFrame = (u.animFrame + 1) % 5;
            } else {
                u.animFrame = 0;
            }
        }

        switch (u.state) {
        case USTATE_IDLE:
            // Military units: auto-attack nearby enemies
            if (u.type != UNIT_VILLAGER) {
                int enemy = unit_find_nearest_enemy(i);
                if (enemy >= 0) {
                    u.attackTarget = enemy;
                    u.state = USTATE_ATTACKING;
                }
            }
            break;

        case USTATE_MOVING:
            unit_step_path(u, terrain);
            // If path done, check what we should do next
            if (u.state == USTATE_IDLE) {
                if (u.gatherTX >= 0 && u.gatherTY >= 0 && u.type == UNIT_VILLAGER) {
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
        }

        // Update pop count
        // (done in game_update via game_update_pop_cap)
    }

    // Recalculate pop counts
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gs.players[p].popCount = unit_count(p);
    }
}
