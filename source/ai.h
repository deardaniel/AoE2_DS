#pragma once
#include "config.h"

struct GameState;
struct TerrainMap;

enum AIStrategy {
    AI_STRAT_BALANCED = 0,  // default balanced play
    AI_STRAT_RUSH     = 1,  // early aggression, fewer vils
    AI_STRAT_BOOM     = 2,  // heavy economy, late army
    AI_STRAT_TURTLE   = 3,  // defensive, towers + walls
    AI_STRAT_COUNT    = 4,
};

void ai_init();
void ai_set_strategy(int strat); // set current strategy
int  ai_get_strategy();          // get current strategy
void ai_update(GameState& gs, TerrainMap& terrain); // call every frame, internally throttles
