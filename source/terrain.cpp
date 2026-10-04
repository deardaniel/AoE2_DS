#include "terrain.h"
#include "fog.h"
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

// Ground texture set for each terrain type — the order of TERRAIN_SETS in
// scripts/shared_constants.py. Mines and berry bushes stand on grass.
enum { SET_GRASS, SET_DIRT, SET_WATER, SET_FOREST, SET_FARM };
static const u8 TERRAIN_SET[TERRAIN_COUNT] = {
    SET_GRASS, SET_DIRT, SET_WATER, SET_FOREST,
    SET_GRASS,  // gold
    SET_GRASS,  // stone
    SET_FARM,
    SET_GRASS,  // berries
};

// A ground texture repeats every TERRAIN_PATTERN tiles; terrain_tiles.bin
// holds every tile of that pattern for every set
enum { TERRAIN_PATTERN = 10 };

// ---------------------------------------------------------------------------
// Terrain edge blending
// ---------------------------------------------------------------------------
struct BlendEdge {
    u8 xs[ISO_TILE_H];  // blend strip start x per row
    u8 xe[ISO_TILE_H];  // blend strip end x per row (exclusive)
};

enum { EDGE_TL = 0, EDGE_TR = 1, EDGE_BL = 2, EDGE_BR = 3 };
static BlendEdge blendEdges[4];      // cross-terrain blend (5px)

// Each diamond edge maps to one neighbor tile offset
static const s8 EDGE_NEIGHBOR[4][2] = {
    {-1,  0},  // top-left     → (tx-1, ty)
    { 0, -1},  // top-right    → (tx, ty-1)
    { 0, +1},  // bottom-left  → (tx, ty+1)
    {+1,  0},  // bottom-right → (tx+1, ty)
};

static void initBlendSet(BlendEdge edges[4], int maxStrip) {
    for (int py = 0; py < ISO_TILE_H; py++) {
        int dxs = ISO_DIAMOND_XSTART[py];
        int dxe = ISO_DIAMOND_XEND[py];
        int width = dxe - dxs;
        int strip = (width < maxStrip) ? width : maxStrip;

        bool topHalf = (py < ISO_TILE_H / 2);

        edges[EDGE_TL].xs[py] = topHalf ? dxs : 0;
        edges[EDGE_TL].xe[py] = topHalf ? dxs + strip : 0;

        edges[EDGE_TR].xs[py] = topHalf ? dxe - strip : 0;
        edges[EDGE_TR].xe[py] = topHalf ? dxe : 0;

        edges[EDGE_BL].xs[py] = topHalf ? 0 : dxs;
        edges[EDGE_BL].xe[py] = topHalf ? 0 : dxs + strip;

        edges[EDGE_BR].xs[py] = topHalf ? 0 : dxe - strip;
        edges[EDGE_BR].xe[py] = topHalf ? 0 : dxe;
    }
}

static void terrain_initBlend() {
    initBlendSet(blendEdges, 5);
}

// Tile graphics for a terrain type at a map position: the piece of that
// terrain's ground texture which lies under the tile. Neighbouring tiles of
// one terrain continue each other, so there are no seams to hide.
static inline const u8* getTileGfx(int tx, int ty, u8 ttype) {
    int tile = TERRAIN_SET[ttype] * (TERRAIN_PATTERN * TERRAIN_PATTERN) +
               (tx % TERRAIN_PATTERN) * TERRAIN_PATTERN + (ty % TERRAIN_PATTERN);
    return terrain_tiles_bin + tile * (ISO_TILE_W * ISO_TILE_H);
}

// Blend neighbor terrain edges onto the current tile
static void blendTileEdges(u8* vram, int tx, int ty, u8 ttype,
                           int dstX, int dstY, const TerrainMap& map) {
    s8 myPri = TERRAIN_BLEND_PRIORITY[ttype];
    if (myPri < 0) return;  // this tile doesn't participate in blending

    // Skip blending if this tile is unexplored
    if (!fogMap.isExplored(0, tx, ty)) return;

    for (int e = 0; e < 4; e++) {
        int nx = tx + EDGE_NEIGHBOR[e][0];
        int ny = ty + EDGE_NEIGHBOR[e][1];
        if (nx < 0 || nx >= MAP_TILES || ny < 0 || ny >= MAP_TILES) continue;

        // Skip blending with unexplored neighbors
        if (!fogMap.isExplored(0, nx, ny)) continue;

        u8 ntype = map.tiles[ny][nx];
        s8 nPri = TERRAIN_BLEND_PRIORITY[ntype];
        if (nPri < 0) continue;  // neighbor doesn't participate

        // A neighbour with higher priority bleeds over this tile's edge
        if (nPri <= myPri) continue;

        // The neighbour's ground as it would look on this tile
        const u8* nsrc = getTileGfx(tx, ty, ntype);
        const BlendEdge& be = blendEdges[e];

        for (int py = 0; py < ISO_TILE_H; py++) {
            int bxs = be.xs[py];
            int bxe = be.xe[py];
            if (bxs >= bxe) continue;

            int screenY = dstY + py;
            if (screenY < 0 || screenY >= SCREEN_H) continue;

            for (int px = bxs; px < bxe; px++) {
                int screenX = dstX + px;
                if (screenX < 0 || screenX >= SCREEN_W) continue;

                // Compute distance from the outer edge
                int dist;
                if (e == EDGE_TL || e == EDGE_BL) {
                    dist = px - bxs;       // left edges: 0 at leftmost
                } else {
                    dist = bxe - 1 - px;   // right edges: 0 at rightmost
                }

                // Graduated dither: 0-1px always, 2px=75%, 3px=50%, 4px=25%
                u32 hash = (u32)(screenX * 7 + screenY * 13 + tx + ty);
                if (dist >= 2 && (int)(hash & 3) < (dist - 1)) continue;

                vram[screenY * 256 + screenX] = nsrc[py * ISO_TILE_W + px];
            }
        }
    }
}

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
    // Tiles are read straight from terrain_tiles_bin (see getTileGfx)
    terrain_initBlend();
}

// ---------------------------------------------------------------------------
// Procedural map generation
// ---------------------------------------------------------------------------
void TerrainMap::generate(u32 seed) {
    rngState = seed ? seed : 12345;
    showTileGrid = false;

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
    // Player 0 (top-left): forest, gold, berries nearby
    tiles[2][8]  = TERRAIN_FOREST; resourceAmt[2][8]  = FOREST_RESOURCE_AMT;
    tiles[3][8]  = TERRAIN_FOREST; resourceAmt[3][8]  = FOREST_RESOURCE_AMT;
    tiles[4][8]  = TERRAIN_FOREST; resourceAmt[4][8]  = FOREST_RESOURCE_AMT;
    tiles[7][3]  = TERRAIN_GOLD;   resourceAmt[7][3]  = GOLD_RESOURCE_AMT;
    tiles[7][4]  = TERRAIN_GOLD;   resourceAmt[7][4]  = GOLD_RESOURCE_AMT;
    tiles[8][2]  = TERRAIN_STONE;  resourceAmt[8][2]  = STONE_RESOURCE_AMT;
    tiles[8][3]  = TERRAIN_STONE;  resourceAmt[8][3]  = STONE_RESOURCE_AMT;

    // Berry patches near player 0 TC
    tiles[1][7]  = TERRAIN_BERRIES; resourceAmt[1][7]  = BERRIES_RESOURCE_AMT;
    tiles[2][7]  = TERRAIN_BERRIES; resourceAmt[2][7]  = BERRIES_RESOURCE_AMT;
    tiles[3][7]  = TERRAIN_BERRIES; resourceAmt[3][7]  = BERRIES_RESOURCE_AMT;
    tiles[4][7]  = TERRAIN_BERRIES; resourceAmt[4][7]  = BERRIES_RESOURCE_AMT;

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

    // Berry patches near player 1 TC
    bx = MAP_TILES - 8; by = MAP_TILES - 2;
    tiles[by][bx]   = TERRAIN_BERRIES; resourceAmt[by][bx]   = BERRIES_RESOURCE_AMT;
    tiles[by-1][bx] = TERRAIN_BERRIES; resourceAmt[by-1][bx] = BERRIES_RESOURCE_AMT;
    tiles[by-2][bx] = TERRAIN_BERRIES; resourceAmt[by-2][bx] = BERRIES_RESOURCE_AMT;
    tiles[by-3][bx] = TERRAIN_BERRIES; resourceAmt[by-3][bx] = BERRIES_RESOURCE_AMT;


    // Random berry patches (1-2 neutral patches elsewhere)
    int numBerries = rngRange(1, 2);
    for (int i = 0; i < numBerries; i++) {
        int cx = rngRange(10, MAP_TILES - 11);
        int cy = rngRange(10, MAP_TILES - 11);
        int count = rngRange(3, 4);
        for (int j = 0; j < count; j++) {
            int tx = cx + rngRange(-1, 1);
            int ty = cy + rngRange(-1, 1);
            if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
            if (tiles[ty][tx] != TERRAIN_GRASS && tiles[ty][tx] != TERRAIN_DIRT) continue;
            tiles[ty][tx] = TERRAIN_BERRIES;
            resourceAmt[ty][tx] = BERRIES_RESOURCE_AMT;
        }
    }
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

            // For grass/dirt tiles, select a variant based on tile position
            // Uses a simple hash to pick deterministically
            const u8* src = getTileGfx(tx, ty, ttype);

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

                // When grid is hidden, extend diamond 1px on each side
                // to fill the gap between adjacent tiles, clamping source reads
                int renderXs = xs;
                int renderXe = xe;
                if (!showTileGrid) {
                    renderXs = (xs > 0) ? xs - 1 : xs;
                    renderXe = (xe < ISO_TILE_W) ? xe + 1 : xe;
                }

                // Clip to screen horizontally
                int drawXs = dstX + renderXs;
                int drawXe = dstX + renderXe;
                int srcOff = renderXs;
                if (drawXs < 0) { srcOff -= drawXs; drawXs = 0; }
                if (drawXe > SCREEN_W) drawXe = SCREEN_W;
                if (drawXs >= drawXe) continue;

                u8* dst = &vram[screenY * 256 + drawXs];
                if (showTileGrid) {
                    // Simple memcpy when grid is shown
                    memcpy(dst, &src[py * ISO_TILE_W + srcOff], drawXe - drawXs);
                } else {
                    // Copy with edge clamping for extended pixels
                    for (int px = drawXs; px < drawXe; px++) {
                        int srcX = (px - dstX);
                        // Clamp to diamond interior
                        if (srcX < xs) srcX = xs;
                        if (srcX >= xe) srcX = xe - 1;
                        *dst++ = src[py * ISO_TILE_W + srcX];
                    }
                }
            }

            // Blend higher-priority neighbor terrain onto this tile's edges
            blendTileEdges(vram, tx, ty, ttype, dstX, dstY, *this);
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
        // Resource exhausted — show dirt patch (was grass before)
        tiles[ty][tx] = TERRAIN_DIRT;
        resourceAmt[ty][tx] = 0;
    }

    return gathered;
}

void TerrainMap::setTile(int tx, int ty, u8 type, int resAmt) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return;
    tiles[ty][tx] = type;
    resourceAmt[ty][tx] = resAmt;
}
