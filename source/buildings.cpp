#include "buildings.h"
#include "game.h"
#include "units.h"
#include "terrain.h"
#include "sound.h"
#include "projectiles.h"
#include "tech.h"
#include <string.h>

Building buildings[MAX_BUILDINGS];

void buildings_init() {
    memset(buildings, 0, sizeof(buildings));
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        buildings[i].alive = false;
        buildings[i].oamSlot = -1;
        buildings[i].attackTargetUnit = -1;
        for (int q = 0; q < 3; q++) buildings[i].trainQueue[q] = -1;
        buildings[i].garrisonCount = 0;
        for (int g = 0; g < MAX_GARRISON; g++) buildings[i].garrison[g] = -1;
        buildings[i].rallyTX = -1;
        buildings[i].rallyTY = -1;
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
    b.hp = (st.hp / 10 > 0) ? st.hp / 10 : 1;  // rises with construction
    b.buildProgress = 0;
    b.trainProgress = 0;
    b.spriteGfx = NULL;
    b.oamSlot = -1;
    b.attackCooldown = 0;
    b.attackTargetUnit = -1;
    for (int q = 0; q < 3; q++) b.trainQueue[q] = -1;
    b.garrisonCount = 0;
    for (int g = 0; g < MAX_GARRISON; g++) b.garrison[g] = -1;
    b.rallyTX = -1;
    b.rallyTY = -1;
    if (type == BLDG_FARM) {
        terrain.setTile(tileX, tileY, TERRAIN_FARM, playerFarmFood[owner]);
    }

    if (owner == 0) sound_play(SFX_BUILDING_PLACE);

    return slot;
}

void building_complete_now(int idx) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return;
    buildings[idx].buildProgress = BLDG_STATS[buildings[idx].type].buildTime;
    buildings[idx].hp = BLDG_STATS[buildings[idx].type].hp;
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

        // Construction progress — only advances via villagers in USTATE_BUILDING
        // (see unit_update_building in units.cpp)
        if (b.buildProgress < st.buildTime) {
            continue; // Can't train while building
        }

        // Farm auto-reseed: if completed farm's terrain tile has reverted to grass,
        // automatically replant if owner can afford it
        if (b.type == BLDG_FARM) {
            int tx = b.x / TILE_PX;
            int ty = b.y / TILE_PX;
            if (terrain.tileAt(tx, ty) != TERRAIN_FARM) {
                if (game_can_afford(gs, b.owner, BLDG_STATS[BLDG_FARM].cost)) {
                    game_deduct_cost(gs, b.owner, BLDG_STATS[BLDG_FARM].cost);
                    terrain.setTile(tx, ty, TERRAIN_FARM, playerFarmFood[b.owner]);
                }
            }
        }

        // Ranged building attack: TC, Tower, Castle
        int bldgRange = 0, bldgDmg = 0, bldgCooldown = 0;
        if (b.type == BLDG_TOWN_CENTER)  { bldgRange = 6; bldgDmg = 5 + b.garrisonCount * 2; bldgCooldown = 60; }
        else if (b.type == BLDG_TOWER)   { bldgRange = 7; bldgDmg = 5; bldgCooldown = 60; }
        else if (b.type == BLDG_CASTLE)  { bldgRange = 8; bldgDmg = 11; bldgCooldown = 45; }

        if (bldgRange > 0) {
            if (b.attackCooldown > 0) {
                b.attackCooldown--;
            } else {
                int rangePx = bldgRange * TILE_PX;
                int bestDist = rangePx * rangePx + 1;
                int bestEnemy = -1;
                int cx = b.x + st.tileW * TILE_PX / 2 - TILE_PX / 2;
                int cy = b.y + st.tileH * TILE_PX / 2 - TILE_PX / 2;
                for (int j = 0; j < MAX_UNITS; j++) {
                    if (!units[j].alive || units[j].state == USTATE_DEAD) continue;
                    if (units[j].state == USTATE_GARRISONED) continue;  // safe inside
                    if (units[j].owner == b.owner || units[j].type == UNIT_SHEEP) continue;
                    int dx = units[j].x - cx;
                    int dy = units[j].y - cy;
                    int dist = dx*dx + dy*dy;
                    if (dist < bestDist) {
                        bestDist = dist;
                        bestEnemy = j;
                    }
                }
                if (bestEnemy >= 0) {
                    const BuildingStats& bst = BLDG_STATS[b.type];
                    s16 bCenterX = b.x + bst.tileW * TILE_PX / 2;
                    s16 bCenterY = b.y + bst.tileH * TILE_PX / 2;
                    projectile_spawn(bCenterX, bCenterY,
                                     units[bestEnemy].x, units[bestEnemy].y,
                                     15, bestEnemy, bldgDmg, b.owner);
                    b.attackCooldown = bldgCooldown;
                    b.attackTargetUnit = bestEnemy;
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

                // Try tiles around the building (skip occupied tiles to avoid stacking)
                for (int dy = -1; dy <= bh && !spawned; dy++) {
                    for (int dx = -1; dx <= bw && !spawned; dx++) {
                        if (dx >= 0 && dx < bw && dy >= 0 && dy < bh) continue;
                        int tx = bx + dx;
                        int ty = by + dy;
                        if (terrain.passable(tx, ty) && !tile_has_unit(tx, ty)) {
                            if (gs.players[b.owner].popCount < gs.players[b.owner].popCap) {
                                int uid = unit_spawn(unitType, b.owner, tx * TILE_PX, ty * TILE_PX);
                                if (uid >= 0) {
                                    spawned = true;
                                    tile_mark_unit(tx, ty, uid);
                                    // Apply rally point
                                    if (b.rallyTX >= 0 && b.rallyTY >= 0) {
                                        u8 rtt = terrain.tileAt(b.rallyTX, b.rallyTY);
                                        bool isResource = (rtt == TERRAIN_FOREST || rtt == TERRAIN_GOLD ||
                                                           rtt == TERRAIN_STONE || rtt == TERRAIN_FARM ||
                                                           rtt == TERRAIN_BERRIES);
                                        int rallyBldg = building_at_tile(b.rallyTX, b.rallyTY);
                                        bool isIncompleteBldg = (rallyBldg >= 0 &&
                                            !building_is_complete(rallyBldg) &&
                                            buildings[rallyBldg].owner == b.owner);
                                        if (isResource && unitType == UNIT_VILLAGER) {
                                            unit_command_gather(uid, b.rallyTX, b.rallyTY, terrain);
                                        } else if (isIncompleteBldg && unitType == UNIT_VILLAGER) {
                                            unit_command_build(uid, rallyBldg, terrain);
                                        } else {
                                            unit_command_move(uid, b.rallyTX * TILE_PX + TILE_PX / 2,
                                                              b.rallyTY * TILE_PX + TILE_PX / 2, terrain);
                                        }
                                    }
                                }
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

    // Trigger "under attack" alert for player 0
    if (buildings[idx].owner == 0) {
        extern GameState gameState;
        if (gameState.underAttackTimer == 0) {
            gameState.underAttackTimer = 180;
            gameState.attackAlertTX = buildings[idx].x / TILE_PX;
            gameState.attackAlertTY = buildings[idx].y / TILE_PX;
        }
    }
}

void building_destroy(int idx, TerrainMap& terrain) {
    if (idx < 0 || idx >= MAX_BUILDINGS || !buildings[idx].alive) return;
    Building& b = buildings[idx];
    // Track end-game stats — opposing player gets credit
    extern GameState gameState;
    gameState.bldgsDestroyed[1 - b.owner]++;

    // Eject garrisoned units before destroying
    if (b.garrisonCount > 0) {
        building_ungarrison_all(idx, terrain);
    }

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

    sound_play(SFX_DESTROY);

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

bool building_garrison(int bldgIdx, int unitIdx) {
    if (bldgIdx < 0 || bldgIdx >= MAX_BUILDINGS || !buildings[bldgIdx].alive) return false;
    if (unitIdx < 0 || unitIdx >= MAX_UNITS || !units[unitIdx].alive) return false;
    Building& b = buildings[bldgIdx];
    if (!building_is_complete(bldgIdx)) return false;
    if (b.type != BLDG_TOWN_CENTER) return false; // only TC supports garrison
    if (b.owner != units[unitIdx].owner) return false;
    if (units[unitIdx].type == UNIT_SHEEP) return false; // livestock can't garrison
    if (b.garrisonCount >= MAX_GARRISON) return false;

    // Add to garrison
    for (int g = 0; g < MAX_GARRISON; g++) {
        if (b.garrison[g] < 0) {
            b.garrison[g] = unitIdx;
            b.garrisonCount++;

            // Hide unit (mark as garrisoned — not dead, just invisible)
            Unit& u = units[unitIdx];
            u.state = USTATE_GARRISONED;
            u.x = b.x + BLDG_STATS[b.type].tileW * TILE_PX / 2;
            u.y = b.y + BLDG_STATS[b.type].tileH * TILE_PX / 2;

            // Free sprite resources while garrisoned
            if (u.spriteGfx) {
                oamFreeGfx(&oamSub, u.spriteGfx);
                u.spriteGfx = NULL;
            }
            u.oamSlot = -1;
            return true;
        }
    }
    return false;
}

void building_ungarrison_all(int bldgIdx, TerrainMap& terrain) {
    if (bldgIdx < 0 || bldgIdx >= MAX_BUILDINGS || !buildings[bldgIdx].alive) return;
    Building& b = buildings[bldgIdx];
    if (b.garrisonCount == 0) return;

    int bx = b.x / TILE_PX;
    int by = b.y / TILE_PX;
    int bw = BLDG_STATS[b.type].tileW;
    int bh = BLDG_STATS[b.type].tileH;

    for (int g = 0; g < MAX_GARRISON; g++) {
        if (b.garrison[g] < 0) continue;
        int ui = b.garrison[g];
        if (ui < 0 || ui >= MAX_UNITS || !units[ui].alive) {
            b.garrison[g] = -1;
            continue;
        }

        // Find adjacent tile to place unit: free tiles first, then any passable
        // one (stacking beats losing the unit), then the building's own corner.
        bool placed = false;
        for (int pass = 0; pass < 2 && !placed; pass++) {
            for (int dy = -1; dy <= bh && !placed; dy++) {
                for (int dx = -1; dx <= bw && !placed; dx++) {
                    if (dx >= 0 && dx < bw && dy >= 0 && dy < bh) continue;
                    int tx = bx + dx;
                    int ty = by + dy;
                    if (terrain.passable(tx, ty) && (pass == 1 || !tile_has_unit(tx, ty))) {
                        units[ui].x = tx * TILE_PX;
                        units[ui].y = ty * TILE_PX;
                        tile_mark_unit(tx, ty, ui);
                        placed = true;
                    }
                }
            }
        }
        if (!placed) {
            units[ui].x = b.x;
            units[ui].y = b.y;
        }
        units[ui].state = USTATE_IDLE;
        b.garrison[g] = -1;
    }
    b.garrisonCount = 0;
}
