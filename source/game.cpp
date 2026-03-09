#include "game.h"
#include "buildings.h"
#include "units.h"
#include "sound.h"

void game_init(GameState& gs) {
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gs.players[p].resources[RES_FOOD]  = 200;
        gs.players[p].resources[RES_WOOD]  = 200;
        gs.players[p].resources[RES_GOLD]  = 100;
        gs.players[p].resources[RES_STONE] = 200;
        gs.players[p].age = AGE_DARK;
        gs.players[p].popCount = 0;
        gs.players[p].popCap   = 0;
        gs.players[p].techResearched = 0;
        gs.players[p].ageProgress = -1;
    }

    gs.camX = 0;
    gs.camY = 0;
    gs.followCam = false;
    gs.selectedUnit = -1;
    gs.selectedBldg = -1;
    gs.selectionCount = 0;
    for (int i = 0; i < MAX_UNITS; i++) gs.unitSelected[i] = false;
    gs.phase = PHASE_PLAYING;
    gs.frameCount = 0;
    gs.inputMode = 0;
    gs.placeBldgType = 0;
    gs.buildMenuOpen = false;
    gs.buildMenuPage = 0;
    gs.marketTradeIdx = 0;
    gs.selectedTileX = -1;
    gs.selectedTileY = -1;
    gs.touchActive = false;
    gs.isDragging = false;
    gs.dragStartX = gs.dragStartY = 0;
    gs.dragEndX = gs.dragEndY = 0;
    gs.moveTargetIsoX = 0;
    gs.moveTargetIsoY = 0;
    gs.moveTargetTimer = 0;
    gs.underAttackTimer = 0;
    gs.attackAlertTX = -1;
    gs.attackAlertTY = -1;
    gs.trainUnitType = -1;
    // aiDifficulty preserved across restarts (set by menu, default AI_NORMAL)
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gs.unitsKilled[p] = 0;
        gs.unitsLost[p] = 0;
        gs.bldgsDestroyed[p] = 0;
    }
}

bool game_can_afford(const GameState& gs, int player, const int cost[RES_COUNT]) {
    for (int i = 0; i < RES_COUNT; i++) {
        if (gs.players[player].resources[i] < cost[i]) return false;
    }
    return true;
}

void game_deduct_cost(GameState& gs, int player, const int cost[RES_COUNT]) {
    for (int i = 0; i < RES_COUNT; i++) {
        gs.players[player].resources[i] -= cost[i];
    }
}

void game_refund_cost(GameState& gs, int player, const int cost[RES_COUNT]) {
    for (int i = 0; i < RES_COUNT; i++) {
        gs.players[player].resources[i] += cost[i];
    }
}

void game_add_resource(GameState& gs, int player, int resType, int amount) {
    gs.players[player].resources[resType] += amount;
}

void game_update_pop_cap(GameState& gs, int player) {
    // Recalculate based on houses and town centers
    extern Building buildings[MAX_BUILDINGS];
    int cap = 0;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive || buildings[i].owner != player) continue;
        if (buildings[i].buildProgress < BLDG_STATS[buildings[i].type].buildTime) continue;
        cap += BLDG_STATS[buildings[i].type].popSpace;
    }
    if (cap > MAX_UNITS_PER_PLAYER) cap = MAX_UNITS_PER_PLAYER;
    gs.players[player].popCap = cap;
}

void game_check_win_lose(GameState& gs) {
    if (gs.phase != PHASE_PLAYING) return;

    extern Building buildings[MAX_BUILDINGS];

    // Check if either player has no buildings left
    bool p0has = false, p1has = false;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        if (buildings[i].owner == 0) p0has = true;
        if (buildings[i].owner == 1) p1has = true;
    }

    if (!p1has && p0has) gs.phase = PHASE_VICTORY;
    if (!p0has && p1has) gs.phase = PHASE_DEFEAT;
    if (!p0has && !p1has) gs.phase = PHASE_DEFEAT; // draw = loss
}

void game_update(GameState& gs) {
    gs.frameCount++;

    // Update population caps
    for (int p = 0; p < NUM_PLAYERS; p++) {
        game_update_pop_cap(gs, p);
    }

    // Age research progress
    for (int p = 0; p < NUM_PLAYERS; p++) {
        if (gs.players[p].ageProgress >= 0) {
            gs.players[p].ageProgress++;
            int nextAge = gs.players[p].age + 1;
            if (nextAge < AGE_COUNT && gs.players[p].ageProgress >= AGE_RESEARCH_TIME[nextAge]) {
                gs.players[p].age = nextAge;
                gs.players[p].ageProgress = -1;
                if (p == 0) sound_play(SFX_BUILDING_COMPLETE);
            }
        }
    }

    // Win/lose check every frame
    game_check_win_lose(gs);
}

void game_clear_selection(GameState& gs) {
    gs.selectedUnit = -1;
    gs.selectedBldg = -1;
    gs.selectedTileX = -1;
    gs.selectedTileY = -1;
    gs.selectionCount = 0;
    gs.trainUnitType = -1;
    for (int i = 0; i < MAX_UNITS; i++) gs.unitSelected[i] = false;
}

void game_select_unit(GameState& gs, int unitIdx) {
    game_clear_selection(gs);
    if (unitIdx >= 0 && unitIdx < MAX_UNITS) {
        gs.selectedUnit = unitIdx;
        gs.unitSelected[unitIdx] = true;
        gs.selectionCount = 1;
    }
}

void game_add_to_selection(GameState& gs, int unitIdx) {
    if (unitIdx < 0 || unitIdx >= MAX_UNITS) return;
    if (gs.unitSelected[unitIdx]) return; // already selected
    gs.unitSelected[unitIdx] = true;
    gs.selectionCount++;
    if (gs.selectedUnit < 0) gs.selectedUnit = unitIdx;
}
