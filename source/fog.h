#pragma once
#include "config.h"

// Fog states
enum { FOG_UNEXPLORED = 0, FOG_EXPLORED = 1, FOG_VISIBLE = 2 };

struct FogMap {
    u8 state[NUM_PLAYERS][MAP_TILES][MAP_TILES];

    void init();
    void update(); // recalculate visibility from unit/building positions
    bool isVisible(int player, int tx, int ty) const;
    bool isExplored(int player, int tx, int ty) const;
    void forceExplore(int player, int tx, int ty);
};

extern FogMap fogMap;
