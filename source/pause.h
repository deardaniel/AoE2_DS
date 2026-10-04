#pragma once
#include <nds.h>

struct GameState;
struct TerrainMap;

// Pause menu (START): resume, save, load, music, new game. While it is open
// the game stands still; main.cpp calls pause_frame() instead of running a
// game frame.
enum PauseResult { PAUSE_STAY, PAUSE_RESUME, PAUSE_NEW_GAME };

void pause_open(bool loadPage = false);   // loadPage: straight to the saved games
bool pause_is_open();
// Handle this frame's input (scanKeys() already called) and draw the menu
// over the frame in buf (256x192, 8-bit)
PauseResult pause_frame(u8* buf, GameState& gs, TerrainMap& terrain);
