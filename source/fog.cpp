#include "fog.h"
#include "units.h"
#include "buildings.h"
#include "tech.h"
#include <string.h>

FogMap fogMap;

void FogMap::init() {
    memset(state, FOG_UNEXPLORED, sizeof(state));
    version++;
}

void FogMap::update() {
    static u8 before[MAP_TILES][MAP_TILES];
    memcpy(before, state[0], sizeof(before));

    // Demote visible -> explored for both players
    for (int p = 0; p < NUM_PLAYERS; p++) {
        for (int ty = 0; ty < MAP_TILES; ty++) {
            for (int tx = 0; tx < MAP_TILES; tx++) {
                if (state[p][ty][tx] == FOG_VISIBLE) {
                    state[p][ty][tx] = FOG_EXPLORED;
                }
            }
        }
    }

    // Mark tiles visible around each alive unit
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
        int owner = units[i].owner;
        int los = playerUnitStats[units[i].owner][units[i].type].los;
        int cx = (units[i].x + TILE_PX/2) / TILE_PX;
        int cy = (units[i].y + TILE_PX/2) / TILE_PX;

        for (int dy = -los; dy <= los; dy++) {
            for (int dx = -los; dx <= los; dx++) {
                if (dx*dx + dy*dy > los*los) continue;
                int tx = cx + dx;
                int ty = cy + dy;
                if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES) {
                    state[owner][ty][tx] = FOG_VISIBLE;
                }
            }
        }
    }

    // Mark tiles visible around each alive building
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        int owner = buildings[i].owner;
        int bx = buildings[i].x / TILE_PX;
        int by = buildings[i].y / TILE_PX;
        int los = 5; // buildings have LOS of 5

        for (int dy = -los; dy <= los; dy++) {
            for (int dx = -los; dx <= los; dx++) {
                if (dx*dx + dy*dy > los*los) continue;
                int tx = bx + dx;
                int ty = by + dy;
                if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES) {
                    state[owner][ty][tx] = FOG_VISIBLE;
                }
            }
        }
    }

    if (memcmp(before, state[0], sizeof(before)) != 0) version++;
}

u32 FogMap::viewHash(int minTX, int minTY, int maxTX, int maxTY) const {
    if (minTX < 0) minTX = 0;
    if (minTY < 0) minTY = 0;
    if (maxTX >= MAP_TILES) maxTX = MAP_TILES - 1;
    if (maxTY >= MAP_TILES) maxTY = MAP_TILES - 1;
    u32 h = 2166136261u;
    for (int ty = minTY; ty <= maxTY; ty++)
        for (int tx = minTX; tx <= maxTX; tx++)
            h = (h ^ state[0][ty][tx]) * 16777619u;
    return h;
}

bool FogMap::isVisible(int player, int tx, int ty) const {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return false;
    return state[player][ty][tx] == FOG_VISIBLE;
}

bool FogMap::isExplored(int player, int tx, int ty) const {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return false;
    return state[player][ty][tx] >= FOG_EXPLORED;
}

void FogMap::forceExplore(int player, int tx, int ty) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return;
    if (state[player][ty][tx] == FOG_UNEXPLORED) {
        state[player][ty][tx] = FOG_EXPLORED;
        version++;
    }
}
