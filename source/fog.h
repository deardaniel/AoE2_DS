#pragma once
#include "config.h"

// Fog states
enum { FOG_UNEXPLORED = 0, FOG_EXPLORED = 1, FOG_VISIBLE = 2 };

struct FogMap {
    u8 state[NUM_PLAYERS][MAP_TILES][MAP_TILES];
    u32 version;  // bumped whenever player 0's view changes (redraw the ground)

    void init();
    void update(); // recalculate visibility from unit/building positions
    bool isVisible(int player, int tx, int ty) const;
    bool isExplored(int player, int tx, int ty) const;
    void forceExplore(int player, int tx, int ty);
    // Fingerprint of player 0's fog over a tile rectangle (the part on screen)
    u32  viewHash(int minTX, int minTY, int maxTX, int maxTY) const;
};

extern FogMap fogMap;
