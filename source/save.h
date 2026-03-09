#pragma once

struct TerrainMap;

// Save/load game state to/from fat:/ filesystem
// Returns true on success
bool save_game(TerrainMap& terrain);
bool load_game(TerrainMap& terrain);
bool save_exists();
