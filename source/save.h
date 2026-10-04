#pragma once
#include <nds.h>

struct TerrainMap;

// Saved games: three slots on the SD card the game was started from
// (aoe2dsi_1.sav .. aoe2dsi_3.sav in its root).
enum { SAVE_SLOTS = 3 };

// What a slot holds, for the menu
struct SaveInfo {
    u8  age;
    u8  pop, popCap;
    int frames;       // game time, 60 per second
};

bool save_available();                       // is there a card to save to?
bool save_info(int slot, SaveInfo& info);    // false: empty or unreadable
bool save_game(int slot, TerrainMap& terrain);
bool load_game(int slot, TerrainMap& terrain);
