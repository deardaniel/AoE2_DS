#pragma once
#include "config.h"

// Fog states
enum { FOG_UNEXPLORED = 0, FOG_EXPLORED = 1, FOG_VISIBLE = 2 };
enum { FOG_CHANGED_SHADE = 1, FOG_CHANGED_NEW = 2 };

struct FogMap {
    u8 state[NUM_PLAYERS][MAP_TILES][MAP_TILES];
    u32 version;  // bumped whenever player 0's view changes (redraw the ground)
    // Tiles where player 0's view changed since the ground was last drawn:
    // FOG_CHANGED_SHADE = in or out of sight (only the dimming differs),
    // FOG_CHANGED_NEW = explored for the first time (what stands on it, and
    // the blending of the tiles round it, appear too). main.cpp redraws just
    // those tiles and clears the flags.
    u8 changed[MAP_TILES][MAP_TILES];

    void init();
    void update(); // recalculate visibility from unit/building positions
    bool isVisible(int player, int tx, int ty) const;
    bool isExplored(int player, int tx, int ty) const;
    void forceExplore(int player, int tx, int ty);
};

extern FogMap fogMap;
