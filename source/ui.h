#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

void ui_init();
void ui_update(const GameState& gs, const TerrainMap& terrain);
