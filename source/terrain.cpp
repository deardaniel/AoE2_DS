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
//
// Where two terrains meet, the one with the higher blend priority spills up
// to half a tile onto the other. How far it reaches at each pixel comes from
// a smooth noise field fixed to the map, so the border wanders like the
// game's blend masks do instead of following the tile edges in steps. The
// palette can't mix two colours, so each pixel is one terrain or the other;
// the noise carries a little grain to soften the line.
//
// Water takes the lowest priority, so land always spills onto it. Past the
// land a water tile gets a strip of beach (the dirt texture) and then
// lightened shallows — the shore.
// ---------------------------------------------------------------------------
static u8 tileU[ISO_TILE_H][ISO_TILE_W];   // position across the tile, 0-255,
static u8 tileV[ISO_TILE_H][ISO_TILE_W];   // along the map's x and y axes
enum { NOISE_SIZE = 128 };
static u8 blendNoise[NOISE_SIZE][NOISE_SIZE];
u8 terrainShallowLut[256];                 // a palette entry -> its sunlit-water tint

// The eight neighbours and, for each, how a pixel's distance from that
// neighbour is measured: bit 0-1 = u term (0 none, 1 u, 2 1-u), bit 2-3 = v term
static const s8 BLEND_NEIGHBOR[8][3] = {
    {-1,  0, 1}, {+1,  0, 2}, { 0, -1, 1 << 2}, { 0, +1, 2 << 2},
    {-1, -1, 1 | (1 << 2)}, {+1, -1, 2 | (1 << 2)}, {-1, +1, 1 | (2 << 2)}, {+1, +1, 2 | (2 << 2)},
};

static void terrain_initBlend() {
    for (int py = 0; py < ISO_TILE_H; py++) {
        for (int px = 0; px < ISO_TILE_W; px++) {
            // Inverse of screen = ((tx - ty) * 16, (tx + ty) * 8), in 1/256 tile
            int x = px * 2 + 1 - ISO_TILE_W, y = py * 2 + 1;   // half pixels
            int u = (x * 8 + y * 16) / 2, v = (y * 16 - x * 8) / 2;
            tileU[py][px] = (u < 0) ? 0 : (u > 255) ? 255 : u;
            tileV[py][px] = (v < 0) ? 0 : (v > 255) ? 255 : v;
        }
    }

    // Value noise: a 16x16 lattice of random heights, interpolated, with a
    // quarter of fine grain on top
    static u8 lattice[16][16];
    u32 seed = 0x2545F491;
    for (int i = 0; i < 16 * 16; i++) {
        seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
        lattice[i / 16][i % 16] = seed >> 24;
    }
    for (int y = 0; y < NOISE_SIZE; y++) {
        for (int x = 0; x < NOISE_SIZE; x++) {
            int cx = x / 8, cy = y / 8, fx = x % 8, fy = y % 8;
            int a = lattice[cy][cx], b = lattice[cy][(cx + 1) & 15];
            int c = lattice[(cy + 1) & 15][cx], d = lattice[(cy + 1) & 15][(cx + 1) & 15];
            int top = a * (8 - fx) + b * fx, bot = c * (8 - fx) + d * fx;
            int smooth = (top * (8 - fy) + bot * fy) / 64;
            seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5;
            int n = 40 + (smooth * 3 + (int)(seed >> 24)) * 176 / (4 * 255);
            blendNoise[y][x] = n;   // 40-216: never all one terrain at the edge
        }
    }

    // Shallows tint: each colour moved 40% toward a pale blue-green
    for (int i = 0; i < 256; i++) {
        u16 col = sprite_pal_bin[i * 2] | (sprite_pal_bin[i * 2 + 1] << 8);
        int r = (( col        & 31) * 3 + 14 * 2) / 5;
        int g = (((col >> 5)  & 31) * 3 + 24 * 2) / 5;
        int bl = (((col >> 10) & 31) * 3 + 27 * 2) / 5;
        int best = i, bestDist = 0x7FFFFFFF;
        for (int j = 40; j < 256; j++) {
            u16 o = sprite_pal_bin[j * 2] | (sprite_pal_bin[j * 2 + 1] << 8);
            int dr = (o & 31) - r, dg = ((o >> 5) & 31) - g, db = ((o >> 10) & 31) - bl;
            int dist = dr * dr * 2 + dg * dg * 4 + db * db;
            if (dist < bestDist) { bestDist = dist; best = j; }
        }
        terrainShallowLut[i] = best;
    }
}

// Tile graphics for a terrain type at a map position: the piece of that
// terrain's ground texture which lies under the tile. Neighbouring tiles of
// one terrain continue each other, so there are no seams to hide.
static inline const u8* getTileGfx(int tx, int ty, u8 ttype) {
    int tile = TERRAIN_SET[ttype] * (TERRAIN_PATTERN * TERRAIN_PATTERN) +
               (tx % TERRAIN_PATTERN) * TERRAIN_PATTERN + (ty % TERRAIN_PATTERN);
    return terrain_tiles_bin + tile * (ISO_TILE_W * ISO_TILE_H);
}

// How strongly a set of neighbours (bit n = BLEND_NEIGHBOR[n]) reaches each
// pixel of a tile: 255 at the shared edge or corner, 0 half a tile in. Built
// the first time a combination is met — working this out per pixel while
// drawing made a ground redraw three times slower.
static u8 reachMap[256][ISO_TILE_H * ISO_TILE_W];
static u8 reachSpan[256][ISO_TILE_H][2];   // per row: first and one-past-last pixel reached
static bool reachBuilt[256];

static const u8* reach_for(int mask) {
    u8* out = reachMap[mask];
    if (reachBuilt[mask]) return out;
    reachBuilt[mask] = true;
    for (int py = 0; py < ISO_TILE_H; py++) {
        reachSpan[mask][py][0] = ISO_TILE_W;
        reachSpan[mask][py][1] = 0;
        for (int px = 0; px < ISO_TILE_W; px++) {
            int u = tileU[py][px], v = tileV[py][px], best = 0;
            for (int n = 0; n < 8; n++) {
                if (!(mask & (1 << n))) continue;
                int du = BLEND_NEIGHBOR[n][2] & 3, dv = BLEND_NEIGHBOR[n][2] >> 2;
                int d = (du == 1) ? u : (du == 2) ? 255 - u : 0;
                int e = (dv == 1) ? v : (dv == 2) ? 255 - v : 0;
                if (e > d) d = e;
                int w = 255 - d * 2;
                if (w > best) best = w;
            }
            out[py * ISO_TILE_W + px] = best;
            if (best > 0) {
                if (px < reachSpan[mask][py][0]) reachSpan[mask][py][0] = px;
                reachSpan[mask][py][1] = px + 1;
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Blended tiles are kept: a tile on a border looks the same every time it is
// drawn (the noise is fixed to the map), so it is worked out once into a
// 32x16 block and from then on costs the same as a plain tile. Blocks are
// handed out round-robin from a pool that holds about two screens' worth.
// ---------------------------------------------------------------------------
enum { BLEND_POOL = 320 };
static u8  blendBlock[BLEND_POOL][ISO_TILE_H * ISO_TILE_W];
static u32 blendSig[BLEND_POOL];              // what the block was built from
static u16 blendOwner[BLEND_POOL];            // map tile using it (0xFFFF = none)
static u16 blendSlot[MAP_TILES * MAP_TILES];  // map tile -> block (0xFFFF = none)
static int blendNext = 0;
static bool blendReady = false;

// The tile's graphics with higher-priority neighbours (all eight) spilled
// onto it, or NULL if it has none and is drawn plain.
static const u8* blendedTile(int tx, int ty, u8 ttype, const u8* plain, const TerrainMap& map) {
    s8 myPri = TERRAIN_BLEND_PRIORITY[ttype];
    if (myPri < 0) return NULL;  // this tile doesn't participate in blending
    if (!fogMap.isExplored(0, tx, ty)) return NULL;

    // The neighbours that spill, grouped by terrain set (usually just one)
    int count = 0, masks[3] = {0, 0, 0};
    u8 sets[3] = {0, 0, 0};
    const u8* gfx[3];
    for (int n = 0; n < 8; n++) {
        int nx = tx + BLEND_NEIGHBOR[n][0];
        int ny = ty + BLEND_NEIGHBOR[n][1];
        if (nx < 0 || nx >= MAP_TILES || ny < 0 || ny >= MAP_TILES) continue;
        if (!fogMap.isExplored(0, nx, ny)) continue;
        u8 ntype = map.tiles[ny][nx];
        if (TERRAIN_BLEND_PRIORITY[ntype] <= myPri) continue;
        int k = 0;
        while (k < count && sets[k] != TERRAIN_SET[ntype]) k++;
        if (k == count) {
            if (count == 3) continue;
            sets[count] = TERRAIN_SET[ntype];
            gfx[count] = getTileGfx(tx, ty, ntype);
            count++;
        }
        masks[k] |= 1 << n;
    }
    if (count == 0) return NULL;

    if (!blendReady) {
        memset(blendOwner, 0xFF, sizeof(blendOwner));
        memset(blendSlot, 0xFF, sizeof(blendSlot));
        blendReady = true;
    }
    u32 sig = ((u32)masks[0] | ((u32)masks[1] << 8) | ((u32)masks[2] << 16) | ((u32)ttype << 24)) ^
              ((u32)sets[0] * 0x9E3779B1u + (u32)sets[1] * 0x85EBCA6Bu + (u32)sets[2] * 0xC2B2AE35u);
    int tile = ty * MAP_TILES + tx;
    int slot = blendSlot[tile];
    if (slot != 0xFFFF && blendSig[slot] == sig) return blendBlock[slot];
    if (slot == 0xFFFF) {
        slot = blendNext;
        blendNext = (blendNext + 1) % BLEND_POOL;
        if (blendOwner[slot] != 0xFFFF) blendSlot[blendOwner[slot]] = 0xFFFF;
        blendOwner[slot] = tile;
        blendSlot[tile] = slot;
    }
    blendSig[slot] = sig;

    u8* block = blendBlock[slot];
    memcpy(block, plain, ISO_TILE_H * ISO_TILE_W);
    const u8* reach[3];
    for (int k = 0; k < count; k++) reach[k] = reach_for(masks[k]);
    bool water = (ttype == TERRAIN_WATER);
    const u8* beach = getTileGfx(tx, ty, TERRAIN_DIRT);
    int isoX, isoY;   // the noise is read at the tile's place on the map
    tileToIso(tx, ty, isoX, isoY);

    for (int py = 0; py < ISO_TILE_H; py++) {
        int xs = 0, xe = ISO_TILE_W;
        if (count == 1) {   // only the part of the row the neighbours reach
            xs = reachSpan[masks[0]][py][0];
            xe = reachSpan[masks[0]][py][1];
        }
        const u8* noiseRow = blendNoise[((isoY + py) * 2) & (NOISE_SIZE - 1)];
        for (int px = xs; px < xe; px++) {
            int i = py * ISO_TILE_W + px;
            int best = reach[0][i], bestK = 0;
            for (int k = 1; k < count; k++)
                if (reach[k][i] > best) { best = reach[k][i]; bestK = k; }
            if (best == 0) continue;
            int noise = noiseRow[(isoX + px) & (NOISE_SIZE - 1)];
            if (best > noise) {
                block[i] = gfx[bestK][i];
            } else if (water) {
                if (best + 36 > noise) block[i] = beach[i];
                else if (best + 84 > noise) block[i] = terrainShallowLut[block[i]];
            }
        }
    }
    return block;
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
    version++;
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
            // On a border with a higher-priority terrain: the blended version
            const u8* blended = blendedTile(tx, ty, ttype, src, *this);
            if (blended) src = blended;

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

                // A tile is the whole 32x16 rectangle of its ground texture, so
                // the extra pixel either side of the diamond is simply copied
                memcpy(&vram[screenY * 256 + drawXs], &src[py * ISO_TILE_W + srcOff], drawXe - drawXs);
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
        // Resource exhausted — show dirt patch (was grass before)
        tiles[ty][tx] = TERRAIN_DIRT;
        resourceAmt[ty][tx] = 0;
        version++;
    }

    return gathered;
}

void TerrainMap::setTile(int tx, int ty, u8 type, int resAmt) {
    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) return;
    if (tiles[ty][tx] != type) version++;
    tiles[ty][tx] = type;
    resourceAmt[ty][tx] = resAmt;
}
