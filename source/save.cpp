#include "save.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "fog.h"
#include "tech.h"
#include "ai.h"
#include "projectiles.h"
#include <stdio.h>

static const char SAVE_PATH[] = "fat:/aoe2dsi.sav";
static const u32  SAVE_MAGIC  = 0xA0E2D51;
static const u16  SAVE_VERSION = 3;

extern GameState gameState;

static bool write_all(FILE* f, const void* data, size_t size) {
    return fwrite(data, 1, size, f) == size;
}

static bool read_all(FILE* f, void* data, size_t size) {
    return fread(data, 1, size, f) == size;
}

bool save_game(TerrainMap& terrain) {
    FILE* f = fopen(SAVE_PATH, "wb");
    if (!f) return false;

    bool ok = true;

    // Header
    ok = ok && write_all(f, &SAVE_MAGIC, 4);
    ok = ok && write_all(f, &SAVE_VERSION, 2);

    // Player state
    for (int p = 0; p < NUM_PLAYERS; p++)
        ok = ok && write_all(f, &gameState.players[p], sizeof(Player));
    ok = ok && write_all(f, &gameState.camX, sizeof(int));
    ok = ok && write_all(f, &gameState.camY, sizeof(int));
    ok = ok && write_all(f, &gameState.phase, sizeof(GamePhase));
    ok = ok && write_all(f, &gameState.frameCount, sizeof(int));
    ok = ok && write_all(f, &gameState.aiDifficulty, sizeof(u8));
    ok = ok && write_all(f, gameState.unitsKilled, sizeof(gameState.unitsKilled));
    ok = ok && write_all(f, gameState.unitsLost, sizeof(gameState.unitsLost));
    ok = ok && write_all(f, gameState.bldgsDestroyed, sizeof(gameState.bldgsDestroyed));

    // Units
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        ok = ok && write_all(f, &u.alive, 1);
        if (!u.alive) continue;
        ok = ok && write_all(f, &u.owner, 1);
        ok = ok && write_all(f, &u.type, 1);
        ok = ok && write_all(f, &u.x, sizeof(s16));
        ok = ok && write_all(f, &u.y, sizeof(s16));
        ok = ok && write_all(f, &u.hp, sizeof(s16));
        ok = ok && write_all(f, &u.state, 1);
        ok = ok && write_all(f, &u.direction, 1);
        ok = ok && write_all(f, &u.carryType, 1);
        ok = ok && write_all(f, &u.carryAmount, 1);
        ok = ok && write_all(f, &u.role, 1);
        ok = ok && write_all(f, &u.gatherTX, 1);
        ok = ok && write_all(f, &u.gatherTY, 1);
        ok = ok && write_all(f, &u.attackTarget, 1);
        ok = ok && write_all(f, &u.attackBldgTarget, 1);
        ok = ok && write_all(f, &u.buildTarget, 1);
        ok = ok && write_all(f, &u.stance, 1);
        ok = ok && write_all(f, &u.convertProgress, 1);
        ok = ok && write_all(f, &u.deadTimer, sizeof(u16));
        ok = ok && write_all(f, &u.patrolAX, sizeof(s16));
        ok = ok && write_all(f, &u.patrolAY, sizeof(s16));
        ok = ok && write_all(f, &u.patrolBX, sizeof(s16));
        ok = ok && write_all(f, &u.patrolBY, sizeof(s16));
    }

    // Buildings
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        ok = ok && write_all(f, &b.alive, 1);
        if (!b.alive) continue;
        ok = ok && write_all(f, &b.owner, 1);
        ok = ok && write_all(f, &b.type, 1);
        ok = ok && write_all(f, &b.x, sizeof(s16));
        ok = ok && write_all(f, &b.y, sizeof(s16));
        ok = ok && write_all(f, &b.hp, sizeof(s16));
        ok = ok && write_all(f, &b.buildProgress, sizeof(s16));
        ok = ok && write_all(f, b.trainQueue, sizeof(b.trainQueue));
        ok = ok && write_all(f, &b.trainProgress, sizeof(s16));
        ok = ok && write_all(f, &b.garrisonCount, 1);
        ok = ok && write_all(f, b.garrison, sizeof(b.garrison));
        ok = ok && write_all(f, &b.rallyTX, 1);
        ok = ok && write_all(f, &b.rallyTY, 1);
    }

    // Terrain (tiles + resource amounts)
    ok = ok && write_all(f, terrain.tiles, sizeof(terrain.tiles));
    ok = ok && write_all(f, terrain.resourceAmt, sizeof(terrain.resourceAmt));

    // Fog of war
    ok = ok && write_all(f, fogMap.state, sizeof(fogMap.state));

    // Tech economy state
    ok = ok && write_all(f, playerGatherRate, sizeof(playerGatherRate));
    ok = ok && write_all(f, &playerCarryMax, sizeof(playerCarryMax));
    ok = ok && write_all(f, playerFarmFood, sizeof(playerFarmFood));

    // AI strategy
    int strat = ai_get_strategy();
    ok = ok && write_all(f, &strat, sizeof(int));

    fclose(f);
    return ok;
}

bool load_game(TerrainMap& terrain) {
    FILE* f = fopen(SAVE_PATH, "rb");
    if (!f) return false;

    bool ok = true;
    u32 magic;
    u16 version;
    ok = ok && read_all(f, &magic, 4);
    ok = ok && read_all(f, &version, 2);
    if (!ok || magic != SAVE_MAGIC || version != SAVE_VERSION) {
        fclose(f);
        return false;
    }

    // Player state
    for (int p = 0; p < NUM_PLAYERS; p++)
        ok = ok && read_all(f, &gameState.players[p], sizeof(Player));
    ok = ok && read_all(f, &gameState.camX, sizeof(int));
    ok = ok && read_all(f, &gameState.camY, sizeof(int));
    ok = ok && read_all(f, &gameState.phase, sizeof(GamePhase));
    ok = ok && read_all(f, &gameState.frameCount, sizeof(int));
    ok = ok && read_all(f, &gameState.aiDifficulty, sizeof(u8));
    ok = ok && read_all(f, gameState.unitsKilled, sizeof(gameState.unitsKilled));
    ok = ok && read_all(f, gameState.unitsLost, sizeof(gameState.unitsLost));
    ok = ok && read_all(f, gameState.bldgsDestroyed, sizeof(gameState.bldgsDestroyed));

    // Clear transient state
    gameState.selectedUnit = -1;
    gameState.selectedBldg = -1;
    gameState.selectionCount = 0;
    for (int i = 0; i < MAX_UNITS; i++) gameState.unitSelected[i] = false;
    gameState.inputMode = 0;
    gameState.buildMenuOpen = false;
    gameState.touchActive = false;
    gameState.isDragging = false;
    gameState.moveTargetTimer = 0;
    gameState.underAttackTimer = 0;
    gameState.trainUnitType = -1;
    gameState.selectedTileX = -1;
    gameState.selectedTileY = -1;

    // Units — first clear all
    units_init();
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        ok = ok && read_all(f, &u.alive, 1);
        if (!u.alive) continue;
        ok = ok && read_all(f, &u.owner, 1);
        ok = ok && read_all(f, &u.type, 1);
        ok = ok && read_all(f, &u.x, sizeof(s16));
        ok = ok && read_all(f, &u.y, sizeof(s16));
        ok = ok && read_all(f, &u.hp, sizeof(s16));
        ok = ok && read_all(f, &u.state, 1);
        ok = ok && read_all(f, &u.direction, 1);
        ok = ok && read_all(f, &u.carryType, 1);
        ok = ok && read_all(f, &u.carryAmount, 1);
        ok = ok && read_all(f, &u.role, 1);
        ok = ok && read_all(f, &u.gatherTX, 1);
        ok = ok && read_all(f, &u.gatherTY, 1);
        ok = ok && read_all(f, &u.attackTarget, 1);
        ok = ok && read_all(f, &u.attackBldgTarget, 1);
        ok = ok && read_all(f, &u.buildTarget, 1);
        ok = ok && read_all(f, &u.stance, 1);
        ok = ok && read_all(f, &u.convertProgress, 1);
        ok = ok && read_all(f, &u.deadTimer, sizeof(u16));
        u.herdTarget = -1;
        ok = ok && read_all(f, &u.patrolAX, sizeof(s16));
        ok = ok && read_all(f, &u.patrolAY, sizeof(s16));
        ok = ok && read_all(f, &u.patrolBX, sizeof(s16));
        ok = ok && read_all(f, &u.patrolBY, sizeof(s16));
    }

    // Buildings — first clear all
    buildings_init();
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        ok = ok && read_all(f, &b.alive, 1);
        if (!b.alive) continue;
        ok = ok && read_all(f, &b.owner, 1);
        ok = ok && read_all(f, &b.type, 1);
        ok = ok && read_all(f, &b.x, sizeof(s16));
        ok = ok && read_all(f, &b.y, sizeof(s16));
        ok = ok && read_all(f, &b.hp, sizeof(s16));
        ok = ok && read_all(f, &b.buildProgress, sizeof(s16));
        ok = ok && read_all(f, b.trainQueue, sizeof(b.trainQueue));
        ok = ok && read_all(f, &b.trainProgress, sizeof(s16));
        ok = ok && read_all(f, &b.garrisonCount, 1);
        ok = ok && read_all(f, b.garrison, sizeof(b.garrison));
        ok = ok && read_all(f, &b.rallyTX, 1);
        ok = ok && read_all(f, &b.rallyTY, 1);
    }

    // Terrain
    ok = ok && read_all(f, terrain.tiles, sizeof(terrain.tiles));
    ok = ok && read_all(f, terrain.resourceAmt, sizeof(terrain.resourceAmt));

    // Fog of war
    ok = ok && read_all(f, fogMap.state, sizeof(fogMap.state));

    // Tech economy state
    ok = ok && read_all(f, playerGatherRate, sizeof(playerGatherRate));
    ok = ok && read_all(f, &playerCarryMax, sizeof(playerCarryMax));
    ok = ok && read_all(f, playerFarmFood, sizeof(playerFarmFood));

    // AI strategy
    int strat;
    ok = ok && read_all(f, &strat, sizeof(int));
    ai_set_strategy(strat);

    fclose(f);

    // Recalculate derived state
    for (int p = 0; p < NUM_PLAYERS; p++)
        game_update_pop_cap(gameState, p);
    tech_apply_bonuses(0);
    tech_apply_bonuses(1);
    projectiles_init();

    return ok;
}

bool save_exists() {
    FILE* f = fopen(SAVE_PATH, "rb");
    if (!f) return false;
    fclose(f);
    return true;
}
