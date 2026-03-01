#include "buildings.h"
#include "game.h"
#include "units.h"
#include "terrain.h"
#include <string.h>

Building buildings[MAX_BUILDINGS];

void buildings_init() {
    memset(buildings, 0, sizeof(buildings));
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        buildings[i].alive = false;
        buildings[i].oamSlot = -1;
        for (int q = 0; q < 3; q++) buildings[i].trainQueue[q] = -1;
    }
}

int building_place(u8 type, u8 owner, int tileX, int tileY, GameState& gs, TerrainMap& terrain) {
    const BuildingStats& st = BLDG_STATS[type];

    // Check age requirement
    if (gs.players[owner].age < st.ageReq) return -1;

    // Check cost
    if (!game_can_afford(gs, owner, st.cost)) return -1;

    // Check tiles are buildable
    for (int dy = 0; dy < st.tileH; dy++) {
        for (int dx = 0; dx < st.tileW; dx++) {
            int tx = tileX + dx;
            int ty = tileY + dy;
            if (!terrain.canBuild(tx, ty)) return -1;
            if (building_at_tile(tx, ty) >= 0) return -1;
        }
    }

    // Find free slot
    int slot = -1;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) { slot = i; break; }
    }
    if (slot < 0) return -1;

    // Check building count per player
    int count = 0;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (buildings[i].alive && buildings[i].owner == owner) count++;
    }
    if (count >= MAX_BUILDINGS_PER_PLAYER) return -1;

    // Deduct cost
    game_deduct_cost(gs, owner, st.cost);

    // Place building
    Building& b = buildings[slot];
    b.alive = true;
    b.owner = owner;
    b.type = type;
    b.x = tileX * TILE_PX;
    b.y = tileY * TILE_PX;
    b.hp = st.hp;
    b.buildProgress = 0;
    b.trainProgress = 0;
    b.spriteGfx = NULL;
    b.oamSlot = -1;
    b.attackCooldown = 0;
    for (int q = 0; q < 3; q++) b.trainQueue[q] = -1;

    // Mark terrain tiles for farm
    if (type == BLDG_FARM) {
        terrain.setTile(tileX, tileY, TERRAIN_FARM, FARM_RESOURCE_AMT);
    }

    return slot;
}

void buildings_update(GameState& gs, TerrainMap& terrain) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        if (!b.alive) continue;

        // Check if building destroyed
        if (b.hp <= 0) {
            building_destroy(i, terrain);
            continue;
        }

        const BuildingStats& st = BLDG_STATS[b.type];

        // Construction progress
        if (b.buildProgress < st.buildTime) {
            b.buildProgress++;
            continue; // Can't train while building
        }

        // TC arrow attack: shoot nearest enemy in range
        if (b.type == BLDG_TOWN_CENTER) {
            if (b.attackCooldown > 0) {
                b.attackCooldown--;
            } else {
                int rangePx = 6 * TILE_PX;
                int bestDist = rangePx * rangePx + 1;
                int bestEnemy = -1;
                for (int j = 0; j < MAX_UNITS; j++) {
                    if (!units[j].alive || units[j].state == USTATE_DEAD) continue;
                    if (units[j].owner == b.owner) continue;
                    int dx = units[j].x - b.x;
                    int dy = units[j].y - b.y;
                    int dist = dx*dx + dy*dy;
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestEnemy = j;
                    }
                }
                if (bestEnemy >= 0) {
                    units[bestEnemy].hp -= 5;
                    if (units[bestEnemy].hp <= 0) {
                        unit_kill(bestEnemy);
                    }
                    b.attackCooldown = 60;
                }
            }
        }

        // Training queue
        if (b.trainQueue[0] >= 0) {
            u8 unitType = b.trainQueue[0];
            const UnitStats& ust = UNIT_STATS[unitType];

            b.trainProgress++;
            if (b.trainProgress >= ust.trainTime) {
                // Spawn unit adjacent to building
                int bw = st.tileW;
                int bh = st.tileH;
                int bx = b.x / TILE_PX;
                int by = b.y / TILE_PX;
                bool spawned = false;

                // Try tiles around the building
                for (int dy = -1; dy <= bh && !spawned; dy++) {
                    for (int dx = -1; dx <= bw && !spawned; dx++) {
                        if (dx >= 0 && dx < bw && dy >= 0 && dy < bh) continue;
                        int tx = bx + dx;
                        int ty = by + dy;
                        if (terrain.passable(tx, ty)) {
                            // Check pop cap
                            if (gs.players[b.owner].popCount < gs.players[b.owner].popCap) {
                                int uid = unit_spawn(unitType, b.owner, tx * TILE_PX, ty * TILE_PX);
                                if (uid >= 0) spawned = true;
                            }
                        }
                    }
                }

                if (spawned) {
                    // Shift queue
                    b.trainQueue[0] = b.trainQueue[1];
                    b.trainQueue[1] = b.trainQueue[2];
                    b.trainQueue[2] = -1;
                    b.trainProgress = 0;
                } else {
                    // Can't spawn (no room or pop cap) — keep trying
                    b.trainProgress = ust.trainTime; // stay at max
                }
            }
        }
    }
}

void building_damage(int idx, int amount) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return;
    buildings[idx].hp -= amount;
}

void building_destroy(int idx, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return;
    Building& b = buildings[idx];

    // Revert farm terrain
    if (b.type == BLDG_FARM) {
        int tx = b.x / TILE_PX;
        int ty = b.y / TILE_PX;
        terrain.setTile(tx, ty, TERRAIN_GRASS, 0);
    }

    b.alive = false;
    if (b.spriteGfx) {
        oamFreeGfx(&oamSub, b.spriteGfx);
        b.spriteGfx = NULL;
    }
    b.oamSlot = -1;

    // Clear stale selection
    extern GameState gameState;
    if (gameState.selectedBldg == idx) gameState.selectedBldg = -1;
}

bool building_train(int idx, u8 unitType, GameState& gs) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return false;
    Building& b = buildings[idx];

    // Must be complete
    if (b.buildProgress < BLDG_STATS[b.type].buildTime) return false;

    // Check unit's building requirement
    if (UNIT_STATS[unitType].bldgReq != b.type) return false;

    // Check age requirement
    if (gs.players[b.owner].age < UNIT_STATS[unitType].ageReq) return false;

    // Check cost
    if (!game_can_afford(gs, b.owner, UNIT_STATS[unitType].cost)) return false;

    // Find empty queue slot
    for (int q = 0; q < 3; q++) {
        if (b.trainQueue[q] < 0) {
            b.trainQueue[q] = unitType;
            game_deduct_cost(gs, b.owner, UNIT_STATS[unitType].cost);
            return true;
        }
    }
    return false; // queue full
}

void building_cancel_train(int idx, GameState& gs) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return;
    Building& b = buildings[idx];

    if (b.trainQueue[0] >= 0) {
        // Refund cost of front of queue
        game_refund_cost(gs, b.owner, UNIT_STATS[(u8)b.trainQueue[0]].cost);
        b.trainQueue[0] = b.trainQueue[1];
        b.trainQueue[1] = b.trainQueue[2];
        b.trainQueue[2] = -1;
        b.trainProgress = 0;
    }
}

int building_at_tile(int tx, int ty) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        int bx = buildings[i].x / TILE_PX;
        int by = buildings[i].y / TILE_PX;
        int bw = BLDG_STATS[buildings[i].type].tileW;
        int bh = BLDG_STATS[buildings[i].type].tileH;
        if (tx >= bx && tx < bx + bw && ty >= by && ty < by + bh) return i;
    }
    return -1;
}

int building_nearest(int owner, int type, s16 px, s16 py) {
    int bestDist = 0x7FFFFFFF;
    int bestIdx = -1;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive || buildings[i].owner != (u8)owner) continue;
        if (type >= 0 && buildings[i].type != (u8)type) continue;
        int dx = buildings[i].x - px;
        int dy = buildings[i].y - py;
        int dist = dx*dx + dy*dy;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

int building_nearest_dropoff(int owner, int resType, s16 px, s16 py) {
    int bestDist = 0x7FFFFFFF;
    int bestIdx = -1;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive || buildings[i].owner != (u8)owner) continue;
        if (buildings[i].buildProgress < BLDG_STATS[buildings[i].type].buildTime) continue;

        bool isDropoff = false;
        u8 bt = buildings[i].type;
        if (bt == BLDG_TOWN_CENTER) isDropoff = true; // TC accepts all
        if (bt == BLDG_LUMBER_CAMP && resType == RES_WOOD) isDropoff = true;
        if (bt == BLDG_MINING_CAMP && (resType == RES_GOLD || resType == RES_STONE)) isDropoff = true;

        if (!isDropoff) continue;

        int dx = buildings[i].x - px;
        int dy = buildings[i].y - py;
        int dist = dx*dx + dy*dy;
        if (dist < bestDist) {
            bestDist = dist;
            bestIdx = i;
        }
    }
    return bestIdx;
}

int building_count(int owner, int type) {
    int c = 0;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (buildings[i].alive && buildings[i].owner == (u8)owner && buildings[i].type == (u8)type) c++;
    }
    return c;
}

bool building_is_complete(int idx) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return false;
    return buildings[idx].buildProgress >= BLDG_STATS[buildings[idx].type].buildTime;
}
