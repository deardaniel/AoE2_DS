#pragma once
#include "config.h"

// ---------------------------------------------------------------------------
// Isometric coordinate conversion utilities
// ---------------------------------------------------------------------------
// Tile space (tx, ty): the logical 32x32 grid. All game logic uses this.
// Iso screen space (isoX, isoY): pixel position on the isometric canvas.
//
//   isoX = (tx - ty) * (ISO_TILE_W/2) + ISO_MAP_W/2
//   isoY = (tx + ty) * (ISO_TILE_H/2)
//
// The +ISO_MAP_W/2 offset centers the diamond so isoX is always >= 0.
// ---------------------------------------------------------------------------

// Floor division that handles negative numbers correctly
// (C integer division truncates toward zero, we need toward negative infinity)
static inline int isoFloorDiv(int a, int b) {
    return (a >= 0) ? (a / b) : ((a - b + 1) / b);
}

// Tile center -> iso pixel position (top-left corner of the 32x16 diamond)
static inline void tileToIso(int tx, int ty, int& isoX, int& isoY) {
    isoX = (tx - ty) * (ISO_TILE_W / 2) + ISO_MAP_W / 2;
    isoY = (tx + ty) * (ISO_TILE_H / 2);
}

// World pixel position -> iso pixel position (sub-tile smooth scrolling)
// World coords are (tileX * TILE_PX, tileY * TILE_PX) internally.
// We convert to fractional tile coords first, then to iso.
static inline void worldToIso(int px, int py, int& isoX, int& isoY) {
    // Convert pixel to 256ths-of-tile for precision (TILE_PX = 16, so *16 = *256/16)
    // isoX = (px - py) * (ISO_TILE_W/2) / TILE_PX + ISO_MAP_W/2
    // isoY = (px + py) * (ISO_TILE_H/2) / TILE_PX
    isoX = (px - py) * (ISO_TILE_W / 2) / TILE_PX + ISO_MAP_W / 2;
    isoY = (px + py) * (ISO_TILE_H / 2) / TILE_PX;
}

// Screen pixel + camera -> tile coords (inverse projection for touch input)
static inline void screenToTile(int screenX, int screenY, int camX, int camY,
                                int& tileX, int& tileY) {
    // Convert screen pixel to iso canvas coordinates
    int isoX = screenX + camX;
    int isoY = screenY + camY;

    // Undo the iso projection:
    //   isoX = (tx - ty) * 16 + 512
    //   isoY = (tx + ty) * 8
    // So:
    //   rawX = isoX - 512 = (tx - ty) * 16
    //   rawY = isoY       = (tx + ty) * 8
    // Therefore:
    //   tx = (rawX/16 + rawY/8) / 2 = (rawX + 2*rawY) / 32
    //   ty = (rawY/8 - rawX/16) / 2 = (2*rawY - rawX) / 32
    int rawX = isoX - ISO_MAP_W / 2;
    int rawY = isoY;

    tileX = isoFloorDiv(rawX + 2 * rawY, ISO_TILE_W);
    tileY = isoFloorDiv(2 * rawY - rawX, ISO_TILE_W);
}

// ---------------------------------------------------------------------------
// Diamond mask for 32x16 isometric tile rendering
// For each row (0-15), gives the start and end X pixel of the filled region.
// Pixels outside this range are transparent (off-diamond).
// ---------------------------------------------------------------------------
static const int ISO_DIAMOND_XSTART[ISO_TILE_H] = {
    15, 13, 11, 9, 7, 5, 3, 1,   // top half: narrowing from center
     1,  3,  5, 7, 9, 11, 13, 15  // bottom half: widening back
};

static const int ISO_DIAMOND_XEND[ISO_TILE_H] = {
    16, 18, 20, 22, 24, 26, 28, 30,  // top half
    30, 28, 26, 24, 22, 20, 18, 16   // bottom half
};
