#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

// Initialize HD sprite data — loads palette, sets up sprite sheet pointers
void render_init();

// Render all visible units and buildings as OAM sprites on the sub screen
void render_sprites(const GameState& gs, const TerrainMap& terrain);

// Draw the build menu bar on the bottom of the screen
void render_build_menu(u8* vram, const GameState& gs);
