#include "terrain.h"
#include "iso.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Binary terrain data (linked from data/ via bin2o)
// ---------------------------------------------------------------------------
extern const u8 terrain_pal_bin[];
extern const u32 terrain_pal_bin_size;
extern const u8 terrain_tiles_bin[];
extern const u32 terrain_tiles_bin_size;
extern const u8 sprite_pal_bin[];
extern const u32 sprite_pal_bin_size;

// Resource object sprites (32x32 each, single frame)
extern const u8 spr_tree_bin[];
extern const u8 spr_gold_mine_bin[];
extern const u8 spr_stone_mine_bin[];

u8 tileGfxCache[TERRAIN_COUNT][ISO_TILE_W * ISO_TILE_H];
u8 grassVariantCache[GRASS_VARIANTS][ISO_TILE_W * ISO_TILE_H];

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
    // Load sprite palette as the BG palette — both terrain tiles and software-rendered
    // sprites use BG_PALETTE_SUB, so they must share the same palette.
    // Terrain tiles are indexed against sprite palette colors at indices 16-255.
    dmaCopy(sprite_pal_bin, BG_PALETTE_SUB, 512);

    // Override indices 0-15 with UI colors (for fog overlay, build menu, HP bars, etc.)
    // terrain_pal_bin has the correct UI colors at indices 0-15
    for (int i = 0; i < 16; i++) {
        u16 val = terrain_pal_bin[i * 2] | (terrain_pal_bin[i * 2 + 1] << 8);
        BG_PALETTE_SUB[i] = val;
    }
}

// ---------------------------------------------------------------------------
// Init tile graphics cache from preprocessed binary data
// ---------------------------------------------------------------------------
void TerrainMap::initTileGfx() {
    // terrain_tiles_bin contains:
    //   7 base terrain tiles × 512 bytes = 3584 bytes
    //   4 grass variant tiles × 512 bytes = 2048 bytes
    // Total: 5632 bytes
    int tileSize = ISO_TILE_W * ISO_TILE_H;
    for (int t = 0; t < TERRAIN_COUNT; t++) {
        memcpy(tileGfxCache[t],
               &terrain_tiles_bin[t * tileSize],
               tileSize);
    }
    // Load grass variants (stored after the 7 base tiles)
    for (int v = 0; v < GRASS_VARIANTS; v++) {
        memcpy(grassVariantCache[v],
               &terrain_tiles_bin[(TERRAIN_COUNT + v) * tileSize],
               tileSize);
    }
}

// Get resource sprite data for a terrain type (NULL if none)
const u8* terrain_get_resource_sprite(u8 ttype) {
    switch (ttype) {
    case TERRAIN_FOREST: return spr_tree_bin;
    case TERRAIN_GOLD:   return spr_gold_mine_bin;
    case TERRAIN_STONE:  return spr_stone_mine_bin;
    default: return NULL;
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

    // Clear starting areas (opposite corners) — guaranteed grass for 4x4 TC + buffer
    for (int dy = 0; dy < 8; dy++) {
        for (int dx = 0; dx < 8; dx++) {
            // Player 0: top-left area
            tiles[1 + dy][1 + dx] = TERRAIN_GRASS;
            // Player 1: bottom-right area
            tiles[MAP_TILES - 9 + dy][MAP_TILES - 9 + dx] = TERRAIN_GRASS;
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
    // Clear buffer to black (off-map areas will show as black)
    memset(vram, PAL_BLACK, SCREEN_W * SCREEN_H);

    // Determine visible tile range by converting screen corners to tile coords
    int minTX, minTY, maxTX, maxTY;
    int tmpTX, tmpTY;

    // Check all 4 corners of screen with margin for tile overhang
    screenToTile(0, 0, camX, camY, minTX, minTY);
    maxTX = minTX; maxTY = minTY;

    screenToTile(SCREEN_W, 0, camX, camY, tmpTX, tmpTY);
    if (tmpTX < minTX) minTX = tmpTX;
    if (tmpTX > maxTX) maxTX = tmpTX;
    if (tmpTY < minTY) minTY = tmpTY;
    if (tmpTY > maxTY) maxTY = tmpTY;

    screenToTile(0, SCREEN_H, camX, camY, tmpTX, tmpTY);
    if (tmpTX < minTX) minTX = tmpTX;
    if (tmpTX > maxTX) maxTX = tmpTX;
    if (tmpTY < minTY) minTY = tmpTY;
    if (tmpTY > maxTY) maxTY = tmpTY;

    screenToTile(SCREEN_W, SCREEN_H, camX, camY, tmpTX, tmpTY);
    if (tmpTX < minTX) minTX = tmpTX;
    if (tmpTX > maxTX) maxTX = tmpTX;
    if (tmpTY < minTY) minTY = tmpTY;
    if (tmpTY > maxTY) maxTY = tmpTY;

    // Expand range by 1 tile on each side for partial tiles
    minTX -= 1; minTY -= 1;
    maxTX += 1; maxTY += 1;

    // Iterate tiles in depth order (sum = tx + ty, lower sum = further back)
    int minSum = minTX + minTY;
    int maxSum = maxTX + maxTY;

    for (int sum = minSum; sum <= maxSum; sum++) {
        for (int tx = minTX; tx <= maxTX; tx++) {
            int ty = sum - tx;
            if (ty < minTY || ty > maxTY) continue;

            // Get tile type (out of bounds = skip, drawn as black background)
            if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
            u8 ttype = tiles[ty][tx];

            // For grass tiles, select a variant based on tile position
            // Uses a simple hash to pick deterministically
            const u8* src;
            if (ttype == TERRAIN_GRASS) {
                int variant = ((tx * 7) ^ (ty * 13) ^ (tx + ty)) & (GRASS_VARIANTS - 1);
                src = grassVariantCache[variant];
            } else {
                src = tileGfxCache[ttype];
            }

            // Compute screen position of this tile's top-left corner
            int isoX, isoY;
            tileToIso(tx, ty, isoX, isoY);
            int dstX = isoX - camX;
            int dstY = isoY - camY;

            // Quick bounds check (tile is 32x16)
            if (dstX + ISO_TILE_W <= 0 || dstX >= SCREEN_W) continue;
            if (dstY + ISO_TILE_H <= 0 || dstY >= SCREEN_H) continue;

            // Draw diamond pixels using mask table
            for (int py = 0; py < ISO_TILE_H; py++) {
                int screenY = dstY + py;
                if (screenY < 0 || screenY >= SCREEN_H) continue;

                int xs = ISO_DIAMOND_XSTART[py];
                int xe = ISO_DIAMOND_XEND[py];

                // Clip to screen horizontally
                int drawXs = dstX + xs;
                int drawXe = dstX + xe;
                int srcStart = xs;
                if (drawXs < 0) { srcStart -= drawXs; drawXs = 0; }
                if (drawXe > SCREEN_W) drawXe = SCREEN_W;
                if (drawXs >= drawXe) continue;

                int span = drawXe - drawXs;
                memcpy(&vram[screenY * 256 + drawXs],
                       &src[py * ISO_TILE_W + srcStart],
                       span);
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
