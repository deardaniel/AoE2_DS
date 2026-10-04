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
#include <string.h>
#include <sys/stat.h>

// ---------------------------------------------------------------------------
// Saved games
//
// One routine, transfer(), walks every saved field and either writes it or
// reads it, so the two directions cannot fall out of step. Bump SAVE_VERSION
// whenever the list of fields (or a struct written whole, like Player)
// changes; older files are then refused rather than misread.
//
// Not saved: paths (a unit on the move works its route out again on load),
// selection, and anything derived (population, technology bonuses).
// ---------------------------------------------------------------------------
static const u32 SAVE_MAGIC   = 0xA0E2D51;
static const u16 SAVE_VERSION = 4;

extern GameState gameState;

// The card: a flashcart's (fat:) or the DSi's SD slot (sd:)
static const char* save_device() {
    static const char* device = NULL;
    static bool looked = false;
    if (!looked) {
        looked = true;
        struct stat st;
        if (stat("fat:/", &st) == 0) device = "fat:/";
        else if (stat("sd:/", &st) == 0) device = "sd:/";
    }
    return device;
}

bool save_available() {
    return save_device() != NULL;
}

static bool slot_path(int slot, char* out, size_t size) {
    if (slot < 0 || slot >= SAVE_SLOTS || !save_device()) return false;
    snprintf(out, size, "%saoe2dsi_%d.sav", save_device(), slot + 1);
    return true;
}

static FILE* file;
static bool writing, ok;

static void io(void* data, size_t size) {
    if (!ok) return;
    size_t n = writing ? fwrite(data, 1, size, file) : fread(data, 1, size, file);
    ok = (n == size);
}
#define IO(field) io(&(field), sizeof(field))

// Header: identifies the file and carries the summary the menu shows
static bool transfer_header(SaveInfo& info) {
    u32 magic = SAVE_MAGIC;
    u16 version = SAVE_VERSION;
    IO(magic);
    IO(version);
    IO(info.age);
    IO(info.pop);
    IO(info.popCap);
    IO(info.frames);
    return ok && magic == SAVE_MAGIC && version == SAVE_VERSION;
}

static void transfer_body(TerrainMap& terrain) {
    GameState& gs = gameState;
    for (int p = 0; p < NUM_PLAYERS; p++) IO(gs.players[p]);
    IO(gs.camX);
    IO(gs.camY);
    IO(gs.phase);
    IO(gs.frameCount);
    IO(gs.aiDifficulty);
    IO(gs.unitsKilled);
    IO(gs.unitsLost);
    IO(gs.bldgsDestroyed);

    if (!writing) units_init();
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        IO(u.alive);
        if (!u.alive) continue;
        IO(u.owner); IO(u.type);
        IO(u.x); IO(u.y);
        IO(u.hp);
        IO(u.state); IO(u.direction);
        IO(u.carryType); IO(u.carryAmount); IO(u.role);
        IO(u.gatherTX); IO(u.gatherTY); IO(u.gatherTick);
        IO(u.attackTarget); IO(u.attackBldgTarget); IO(u.attackCooldown);
        IO(u.buildTarget); IO(u.garrisonTarget); IO(u.herdTarget);
        IO(u.stance); IO(u.convertProgress);
        IO(u.deadTimer);
        IO(u.pathDestTX); IO(u.pathDestTY);
        IO(u.finalX); IO(u.finalY);
        IO(u.patrolAX); IO(u.patrolAY); IO(u.patrolBX); IO(u.patrolBY);
    }

    if (!writing) buildings_init();
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        IO(b.alive);
        if (!b.alive) continue;
        IO(b.owner); IO(b.type);
        IO(b.x); IO(b.y);
        IO(b.hp);
        IO(b.buildProgress);
        IO(b.trainQueue); IO(b.trainProgress);
        IO(b.attackCooldown);
        IO(b.garrisonCount); IO(b.garrison);
        IO(b.rallyTX); IO(b.rallyTY);
    }

    IO(terrain.tiles);
    IO(terrain.resourceAmt);
    IO(fogMap.state);

    int strategy = ai_get_strategy();
    IO(strategy);
    if (!writing && ok) ai_set_strategy(strategy);
}

bool save_info(int slot, SaveInfo& info) {
    char path[32];
    if (!slot_path(slot, path, sizeof(path))) return false;
    file = fopen(path, "rb");
    if (!file) return false;
    writing = false;
    ok = true;
    bool good = transfer_header(info);
    fclose(file);
    return good;
}

bool save_game(int slot, TerrainMap& terrain) {
    char path[32];
    if (!slot_path(slot, path, sizeof(path))) return false;
    file = fopen(path, "wb");
    if (!file) return false;
    writing = true;
    ok = true;
    SaveInfo info = { gameState.players[0].age, (u8)gameState.players[0].popCount,
                      (u8)gameState.players[0].popCap, gameState.frameCount };
    transfer_header(info);
    transfer_body(terrain);
    if (fclose(file) != 0) ok = false;
    return ok;
}

bool load_game(int slot, TerrainMap& terrain) {
    char path[32];
    if (!slot_path(slot, path, sizeof(path))) return false;
    file = fopen(path, "rb");
    if (!file) return false;
    writing = false;
    ok = true;
    SaveInfo info;
    if (!transfer_header(info)) {
        fclose(file);
        return false;
    }

    // From here the running game is being replaced
    game_init(gameState);
    projectiles_init();
    transfer_body(terrain);
    fclose(file);

    // Derived state
    tech_apply_bonuses(0);
    tech_apply_bonuses(1);
    for (int p = 0; p < NUM_PLAYERS; p++)
        game_update_pop_cap(gameState, p);
    terrain.version++;      // redraw the ground and everything on it
    fogMap.version++;

    // Units that were walking somewhere set off again (paths aren't saved).
    // Gatherers, builders and fighters pick their work back up by themselves
    // from the states and targets that were.
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        if (!u.alive || u.state != USTATE_MOVING) continue;
        s8 attack = u.attackTarget, attackBldg = u.attackBldgTarget;
        s8 gatherTX = u.gatherTX;
        s16 pax = u.patrolAX, pay = u.patrolAY, pbx = u.patrolBX, pby = u.patrolBY;
        u.state = USTATE_IDLE;
        if (u.garrisonTarget >= 0) {
            unit_command_garrison(i, u.garrisonTarget, terrain, true);
        } else if (u.buildTarget >= 0) {
            unit_command_build(i, u.buildTarget, terrain);
        } else if (attack >= 0 || attackBldg >= 0) {
            u.state = USTATE_ATTACKING;     // closes in on the target again
        } else if (gatherTX >= 0) {
            u.state = (u.carryAmount > 0) ? USTATE_RETURNING : USTATE_GATHERING;
        } else {
            bool exact = (u.finalX >= 0);
            int px = exact ? u.finalX + TILE_PX / 2 : u.pathDestTX * TILE_PX + TILE_PX / 2;
            int py = exact ? u.finalY + TILE_PX / 2 : u.pathDestTY * TILE_PX + TILE_PX / 2;
            unit_command_move(i, px, py, terrain, exact);
            u.patrolAX = pax; u.patrolAY = pay; u.patrolBX = pbx; u.patrolBY = pby;
        }
    }
    return ok;
}
