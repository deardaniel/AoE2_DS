#include "terrain.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Binary terrain data (linked from data/ via bin2o)
// ---------------------------------------------------------------------------
extern const u8 terrain_pal_bin[];
extern const u32 terrain_pal_bin_size;
extern const u8 terrain_tiles_bin[];
extern const u32 terrain_tiles_bin_size;

u8 tileGfxCache[TERRAIN_COUNT][TILE_PX * TILE_PX];

// Simple pseudo-random number generator
static u32 rngState;
static u32 rng() {
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    return rngState;
}
static int rngRange(int lo, int hi) {
    return lo + (int)(rng() % (u32)(hi - lo + 1));
}

// ---------------------------------------------------------------------------
// Initialize palette from preprocessed terrain data
// ---------------------------------------------------------------------------
void terrain_initPalette() {
    // Load the shared terrain palette into BG_PALETTE_SUB
    // This includes UI colors at indices 0-15 and terrain colors at 16-255
    dmaCopy(terrain_pal_bin, BG_PALETTE_SUB, 512);

    // Note: SPRITE_PALETTE_SUB is loaded from HD sprite data in render_init()
}

// ---------------------------------------------------------------------------
// Init tile graphics cache from preprocessed binary data
// ---------------------------------------------------------------------------
void TerrainMap::initTileGfx() {
    // terrain_tiles_bin contains 7 terrain tiles × 16×16 bytes = 1792 bytes
    // Each tile is TILE_PX * TILE_PX = 256 bytes of palette-indexed pixels
    for (int t = 0; t < TERRAIN_COUNT; t++) {
        memcpy(tileGfxCache[t],
               &terrain_tiles_bin[t * TILE_PX * TILE_PX],
               TILE_PX * TILE_PX);
    }
}

// ---------------------------------------------------------------------------
// Procedural map generation
// ---------------------------------------------------------------------------
void TerrainMap::generate(u32 seed) {
    rngState = seed ? seed : 12345;

    // Fill all grass
    memset(tiles, TERRAIN_GRASS, sizeof(tiles));
    memset(resourceAmt, 0, sizeof(resourceAmt));

    // Dirt patches (scattered)
    for (int i = 0; i < 40; i++) {
        int cx = rngRange(2, MAP_TILES - 3);
        int cy = rngRange(2, MAP_TILES - 3);
        int r = rngRange(1, 3);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES) {
                    if (dx * dx + dy * dy <= r * r) {
                        tiles[ty][tx] = TERRAIN_DIRT;
                    }
                }
            }
        }
    }

    // Water body (river running roughly across center)
    {
        int wy = MAP_TILES / 2 - 1;
        for (int x = 3; x < MAP_TILES - 3; x++) {
            int wobble = rngRange(-1, 1);
            wy += wobble;
            if (wy < 3) wy = 3;
            if (wy > MAP_TILES - 4) wy = MAP_TILES - 4;
            for (int dy = -1; dy <= 1; dy++) {
                int ty = wy + dy;
                if (ty >= 0 && ty < MAP_TILES) {
                    tiles[ty][x] = TERRAIN_WATER;
                }
            }
        }
    }

    // Clear starting areas (opposite corners) — guaranteed grass
    for (int dy = 0; dy < 6; dy++) {
        for (int dx = 0; dx < 6; dx++) {
            // Player 0: top-left area
            tiles[1 + dy][1 + dx] = TERRAIN_GRASS;
            // Player 1: bottom-right area
            tiles[MAP_TILES - 7 + dy][MAP_TILES - 7 + dx] = TERRAIN_GRASS;
        }
    }

    // Forest clusters (5-7 clusters)
    int numForests = rngRange(5, 7);
    for (int i = 0; i < numForests; i++) {
        int cx = rngRange(3, MAP_TILES - 4);
        int cy = rngRange(3, MAP_TILES - 4);
        int r = rngRange(2, 3);
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                int tx = cx + dx, ty = cy + dy;
                if (tx < 1 || tx >= MAP_TILES-1 || ty < 1 || ty >= MAP_TILES-1) continue;
                if (dx*dx + dy*dy > r*r) continue;
                if (tiles[ty][tx] != TERRAIN_GRASS && tiles[ty][tx] != TERRAIN_DIRT) continue;
                // Don't place in starting areas
                if (tx <= 7 && ty <= 7) continue;
                if (tx >= MAP_TILES-8 && ty >= MAP_TILES-8) continue;
                tiles[ty][tx] = TERRAIN_FOREST;
                resourceAmt[ty][tx] = FOREST_RESOURCE_AMT;
            }
        }
    }

    // Gold patches (4-6 patches of 2-3 tiles each)
    int numGold = rngRange(4, 6);
    for (int i = 0; i < numGold; i++) {
        int cx = rngRange(4, MAP_TILES - 5);
        int cy = rngRange(4, MAP_TILES - 5);
        int count = rngRange(2, 3);
        for (int j = 0; j < count; j++) {
            int tx = cx + rngRange(-1, 1);
            int ty = cy + rngRange(-1, 1);
            if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
            if (tiles[ty][tx] == TERRAIN_WATER) continue;
            if (tx <= 7 && ty <= 7) continue;
            if (tx >= MAP_TILES-8 && ty >= MAP_TILES-8) continue;
            tiles[ty][tx] = TERRAIN_GOLD;
            resourceAmt[ty][tx] = GOLD_RESOURCE_AMT;
        }
    }

    // Stone patches (4-6 patches)
    int numStone = rngRange(4, 6);
    for (int i = 0; i < numStone; i++) {
        int cx = rngRange(4, MAP_TILES - 5);
        int cy = rngRange(4, MAP_TILES - 5);
        int count = rngRange(2, 3);
        for (int j = 0; j < count; j++) {
            int tx = cx + rngRange(-1, 1);
            int ty = cy + rngRange(-1, 1);
            if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
            if (tiles[ty][tx] == TERRAIN_WATER) continue;
            if (tx <= 7 && ty <= 7) continue;
            if (tx >= MAP_TILES-8 && ty >= MAP_TILES-8) continue;
            tiles[ty][tx] = TERRAIN_STONE;
            resourceAmt[ty][tx] = STONE_RESOURCE_AMT;
        }
    }

    // Place some resources near starting positions for fairness
    // Player 0 (top-left): forest and gold nearby
    tiles[2][8]  = TERRAIN_FOREST; resourceAmt[2][8]  = FOREST_RESOURCE_AMT;
    tiles[3][8]  = TERRAIN_FOREST; resourceAmt[3][8]  = FOREST_RESOURCE_AMT;
    tiles[4][8]  = TERRAIN_FOREST; resourceAmt[4][8]  = FOREST_RESOURCE_AMT;
    tiles[7][3]  = TERRAIN_GOLD;   resourceAmt[7][3]  = GOLD_RESOURCE_AMT;
    tiles[7][4]  = TERRAIN_GOLD;   resourceAmt[7][4]  = GOLD_RESOURCE_AMT;
    tiles[8][2]  = TERRAIN_STONE;  resourceAmt[8][2]  = STONE_RESOURCE_AMT;
    tiles[8][3]  = TERRAIN_STONE;  resourceAmt[8][3]  = STONE_RESOURCE_AMT;

    // Player 1 (bottom-right): mirror resources
    int bx = MAP_TILES - 9, by = MAP_TILES - 3;
    tiles[by][bx]   = TERRAIN_FOREST; resourceAmt[by][bx]   = FOREST_RESOURCE_AMT;
    tiles[by-1][bx] = TERRAIN_FOREST; resourceAmt[by-1][bx] = FOREST_RESOURCE_AMT;
    tiles[by-2][bx] = TERRAIN_FOREST; resourceAmt[by-2][bx] = FOREST_RESOURCE_AMT;
    bx = MAP_TILES - 4; by = MAP_TILES - 8;
    tiles[by][bx]   = TERRAIN_GOLD;   resourceAmt[by][bx]   = GOLD_RESOURCE_AMT;
    tiles[by][bx+1] = TERRAIN_GOLD;   resourceAmt[by][bx+1] = GOLD_RESOURCE_AMT;
    tiles[by-1][bx+1] = TERRAIN_STONE; resourceAmt[by-1][bx+1] = STONE_RESOURCE_AMT;
    tiles[by-1][bx]   = TERRAIN_STONE; resourceAmt[by-1][bx]   = STONE_RESOURCE_AMT;
}

// ---------------------------------------------------------------------------
// Render visible portion of map into 256x192 VRAM bitmap
// ---------------------------------------------------------------------------
void TerrainMap::renderViewport(u8* vram, int camX, int camY) const {
    // Determine visible tile range
    int startTX = camX / TILE_PX;
    int startTY = camY / TILE_PX;
    int offX = camX % TILE_PX; // pixel offset within first tile
    int offY = camY % TILE_PX;

    // We need enough tiles to cover screen + partial edges
    int tilesW = (SCREEN_W / TILE_PX) + 2;
    int tilesH = (SCREEN_H / TILE_PX) + 2;

    for (int ty = 0; ty < tilesH; ty++) {
        for (int tx = 0; tx < tilesW; tx++) {
            int mapTX = startTX + tx;
            int mapTY = startTY + ty;

            // Screen destination for this tile
            int dstX = tx * TILE_PX - offX;
            int dstY = ty * TILE_PX - offY;

            // Get tile type (out of bounds = water)
            u8 ttype = TERRAIN_WATER;
            if (mapTX >= 0 && mapTX < MAP_TILES && mapTY >= 0 && mapTY < MAP_TILES) {
                ttype = tiles[mapTY][mapTX];
            }

            const u8* src = tileGfxCache[ttype];

            // Copy tile pixels, clipping to screen
            for (int py = 0; py < TILE_PX; py++) {
                int screenY = dstY + py;
                if (screenY < 0 || screenY >= SCREEN_H) continue;

                for (int px = 0; px < TILE_PX; px++) {
                    int screenX = dstX + px;
                    if (screenX < 0 || screenX >= SCREEN_W) continue;

                    vram[screenY * 256 + screenX] = src[py * TILE_PX + px];
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Tile queries
// ---------------------------------------------------------------------------
u8 TerrainMap::tileAt(int tx, int ty) const {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return TERRAIN_WATER;
    return tiles[ty][tx];
}

u8 TerrainMap::tileAtPixel(int px, int py) const {
    return tileAt(px / TILE_PX, py / TILE_PX);
}

bool TerrainMap::passable(int tx, int ty) const {
    u8 t = tileAt(tx, ty);
    // Only grass, dirt, and farm tiles are walkable
    // Water, forest, gold, stone are obstacles
    return t == TERRAIN_GRASS || t == TERRAIN_DIRT || t == TERRAIN_FARM;
}

bool TerrainMap::passablePixel(int px, int py) const {
    return passable(px / TILE_PX, py / TILE_PX);
}

bool TerrainMap::canBuild(int tx, int ty) const {
    u8 t = tileAt(tx, ty);
    return t == TERRAIN_GRASS || t == TERRAIN_DIRT;
}

int TerrainMap::depleteResource(int tx, int ty, int amount) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return 0;
    if (resourceAmt[ty][tx] <= 0) return 0;

    int gathered = amount;
    if (gathered > resourceAmt[ty][tx]) gathered = resourceAmt[ty][tx];
    resourceAmt[ty][tx] -= gathered;

    if (resourceAmt[ty][tx] <= 0) {
        // Resource exhausted — revert to grass
        tiles[ty][tx] = TERRAIN_GRASS;
        resourceAmt[ty][tx] = 0;
    }

    return gathered;
}

void TerrainMap::setTile(int tx, int ty, u8 type, int resAmt) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return;
    tiles[ty][tx] = type;
    resourceAmt[ty][tx] = resAmt;
}
