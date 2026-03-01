#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

void ai_init();
void ai_update(GameState& gs, TerrainMap& terrain); // call every frame, internally throttles
