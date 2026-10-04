#pragma once
#include "config.h"

struct TerrainMap {
    u8  tiles[MAP_TILES][MAP_TILES];
    s16 resourceAmt[MAP_TILES][MAP_TILES]; // remaining resource per tile
    bool showTileGrid;  // debug: show diamond grid lines between tiles
    u32  version;       // bumped whenever a tile's type changes (redraw the ground)

    void generate(u32 seed);
    // Draws the part of the view inside the clip rectangle (default: all of it)
    void renderViewport(u8* vram, int camX, int camY,
                        int cx0 = 0, int cy0 = 0, int cx1 = SCREEN_W, int cy1 = SCREEN_H) const;
    void initTileGfx();

    u8   tileAt(int tx, int ty) const;
    u8   tileAtPixel(int px, int py) const;
    bool passable(int tx, int ty) const;
    bool passablePixel(int px, int py) const;
    bool canBuild(int tx, int ty) const;

    // Resource gathering: returns amount actually gathered
    int  depleteResource(int tx, int ty, int amount);

    // Set a tile (e.g., farm placement, resource depletion)
    void setTile(int tx, int ty, u8 type, int resAmt = 0);
};

// Palette entry -> its tint in sunlit shallow water (shore, water glints)
extern u8 terrainShallowLut[256];

// Initialize the palette for procedural terrain/sprite colors
void terrain_initPalette();

// Get resource sprite data for a terrain type (NULL if none)
