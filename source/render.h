#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

// Initialize HD sprite data — loads palette, sets up sprite sheet pointers
void render_init();

// Building under a screen pixel, by its sprite (-1 = none)
int render_pick_building(const GameState& gs, int screenX, int screenY);

// Software-render all visible units and buildings into bitmap buffer
void render_sprites_sw(u8* buf, const GameState& gs, const TerrainMap& terrain);

// Draw the build menu bar on the bottom of the screen
void render_build_menu(u8* vram, const GameState& gs);

// Draw the unit training menu bar on the bottom of the screen
void render_train_menu(u8* vram, const GameState& gs);

// Draw building placement preview (diamond footprint under touch)
void render_placement_preview(u8* buf, const GameState& gs, const TerrainMap& terrain);

// Draw the drag-selection box overlay
void render_drag_box(u8* buf, const GameState& gs);

// Draw move target marker (flashing yellow diamond)
void render_move_target(u8* buf, GameState& gs);

// Draw training progress bars on buildings
void render_training_bars(u8* buf, const GameState& gs);

// Draw age advancement progress bar at top of screen
void render_age_progress(u8* buf, const GameState& gs);
