#include "ai.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "tech.h"

static const int AI_PLAYER = 1;
static int aiTimer = 0;
static int aiStrategy = AI_STRAT_BALANCED;

// Difficulty scaling tables [easy, normal, hard]
static const int AI_TICK_RATE[]    = {90, 60, 45};  // frames between AI ticks
static const int AI_VIL_BASE[]     = {6, 8, 10};    // base villager target
static const int AI_VIL_PER_AGE[]  = {2, 4, 5};     // additional vils per age
static const int AI_ARMY_BASE[]    = {4, 3, 2};      // base army threshold (lower = attacks sooner)
static const int AI_ARMY_PER_AGE[] = {1, 2, 3};      // additional threshold per age

// Strategy modifiers [balanced, rush, boom, turtle]
static const int STRAT_VIL_MOD[]   = {0, -3, 4, 0};    // villager target modifier
static const int STRAT_ARMY_MOD[]  = {0, -2, 3, 1};    // army threshold modifier (lower = earlier attack)
static const int STRAT_FARM_MOD[]  = {0, -1, 2, 0};    // farm target modifier

void ai_init() {
    aiTimer = 0;
    // Pick random strategy
    extern GameState gameState;
    u32 seed = (u32)gameState.frameCount ^ 0xDEAD;
    aiStrategy = seed % AI_STRAT_COUNT;
}

void ai_set_strategy(int strat) {
    if (strat >= 0 && strat < AI_STRAT_COUNT)
        aiStrategy = strat;
}

int ai_get_strategy() {
    return aiStrategy;
}

// Find a buildable tile near a given position
static bool ai_find_build_spot(int nearTX, int nearTY, int w, int h,
                                const TerrainMap& terrain, int& outX, int& outY) {
    for (int r = 0; r < 8; r++) {
        for (int ty = nearTY - r; ty <= nearTY + r; ty++) {
            for (int tx = nearTX - r; tx <= nearTX + r; tx++) {
                if (tx < 0 || ty < 0 || tx + w > MAP_TILES || ty + h > MAP_TILES) continue;
                bool ok = true;
                for (int dy = 0; dy < h && ok; dy++) {
                    for (int dx = 0; dx < w && ok; dx++) {
                        if (!terrain.canBuild(tx + dx, ty + dy)) ok = false;
                        if (building_at_tile(tx + dx, ty + dy) >= 0) ok = false;
                    }
                }
                if (ok) { outX = tx; outY = ty; return true; }
            }
        }
    }
    return false;
}

// Find nearest resource tile of a given type
static bool ai_find_resource(int nearTX, int nearTY, u8 terrType,
                             const TerrainMap& terrain, int& outX, int& outY) {
    int bestDist = 0x7FFFFFFF;
    bool found = false;
    for (int ty = 0; ty < MAP_TILES; ty++) {
        for (int tx = 0; tx < MAP_TILES; tx++) {
            if (terrain.tileAt(tx, ty) == terrType) {
                int dx = tx - nearTX;
                int dy = ty - nearTY;
                int dist = dx*dx + dy*dy;
                if (dist < bestDist) {
                    bestDist = dist;
                    outX = tx;
                    outY = ty;
                    found = true;
                }
            }
        }
    }
    return found;
}

// Place a building and send nearest idle villager to build it
static int ai_place_and_build(int type, int bx, int by, GameState& gs, TerrainMap& terrain) {
    int slot = building_place(type, AI_PLAYER, bx, by, gs, terrain);
    if (slot >= 0) {
        int vil = unit_find_idle_villager(AI_PLAYER, 0);
        if (vil >= 0) {
            unit_command_build(vil, slot, terrain);
        }
    }
    return slot;
}

void ai_update(GameState& gs, TerrainMap& terrain) {
    int diff = gs.aiDifficulty;
    if (diff < 0 || diff > 2) diff = 1;

    aiTimer++;
    if (aiTimer < AI_TICK_RATE[diff]) return;
    aiTimer = 0;

    if (gs.phase != PHASE_PLAYING) return;

    Player& p = gs.players[AI_PLAYER];
    int vilCount = unit_count_type(AI_PLAYER, UNIT_VILLAGER);
    int militaryCount = unit_count(AI_PLAYER) - vilCount;

    // Find AI's TC
    int tcIdx = building_nearest(AI_PLAYER, BLDG_TOWN_CENTER, MAP_PX/2, MAP_PX/2);
    int tcTX = MAP_TILES - 4, tcTY = MAP_TILES - 4;
    if (tcIdx >= 0) {
        tcTX = buildings[tcIdx].x / TILE_PX;
        tcTY = buildings[tcIdx].y / TILE_PX;
    }

    // ---- Economy phase ----

    // Train villagers if under target (scale with age, difficulty, and strategy)
    int vilTarget = AI_VIL_BASE[diff] + p.age * AI_VIL_PER_AGE[diff] + STRAT_VIL_MOD[aiStrategy];
    if (vilTarget < 3) vilTarget = 3;
    if (vilCount < vilTarget && tcIdx >= 0 && building_is_complete(tcIdx)) {
        building_train(tcIdx, UNIT_VILLAGER, gs);
    }

    // Assign idle villagers to resources based on need
    for (int tries = 0; tries < 4; tries++) {
        int vil = unit_find_idle_villager(AI_PLAYER, 0);
        if (vil < 0) break;

        int resTX = 0, resTY = 0;
        bool assigned = false;

        // Find the most-needed resource and assign to it
        int lowestRes = 0;
        int lowestVal = p.resources[RES_FOOD];
        for (int r = 1; r < RES_COUNT; r++) {
            if (p.resources[r] < lowestVal) {
                lowestVal = p.resources[r];
                lowestRes = r;
            }
        }

        // Map resource type to terrain type
        // For food, prefer berries (free) over farms (cost wood)
        u8 terrTypes[] = { TERRAIN_FARM, TERRAIN_FOREST, TERRAIN_GOLD, TERRAIN_STONE };
        if (lowestRes == RES_FOOD) {
            // Try sheep first (highest food value), then berries, then farms
            int sheepIdx = -1;
            int sheepBestDist = 0x7FFFFFFF;
            for (int si = 0; si < MAX_UNITS; si++) {
                if (!units[si].alive || units[si].state == USTATE_DEAD) continue;
                if (units[si].type != UNIT_SHEEP || units[si].owner != AI_PLAYER) continue;
                int dx = units[si].x / TILE_PX - tcTX;
                int dy = units[si].y / TILE_PX - tcTY;
                int dist = dx*dx + dy*dy;
                if (dist < sheepBestDist) {
                    sheepBestDist = dist;
                    sheepIdx = si;
                }
            }
            if (sheepIdx >= 0) {
                unit_command_attack(vil, sheepIdx);
                assigned = true;
            } else if (ai_find_resource(tcTX, tcTY, TERRAIN_BERRIES, terrain, resTX, resTY)) {
                unit_command_gather(vil, resTX, resTY, terrain);
                assigned = true;
            }
        }
        if (!assigned && ai_find_resource(tcTX, tcTY, terrTypes[lowestRes], terrain, resTX, resTY)) {
            unit_command_gather(vil, resTX, resTY, terrain);
            assigned = true;
        }

        // Fallback: try any available resource
        if (!assigned) {
            for (int r = 0; r < RES_COUNT; r++) {
                if (ai_find_resource(tcTX, tcTY, terrTypes[r], terrain, resTX, resTY)) {
                    unit_command_gather(vil, resTX, resTY, terrain);
                    assigned = true;
                    break;
                }
            }
        }
    }

    // ---- Building phase ----

    // Send idle villagers to incomplete buildings first
    for (int bi = 0; bi < MAX_BUILDINGS; bi++) {
        if (!buildings[bi].alive || buildings[bi].owner != AI_PLAYER) continue;
        if (building_is_complete(bi)) continue;
        // Check if any villager is already building this
        bool hasBuilder = false;
        for (int ui = 0; ui < MAX_UNITS; ui++) {
            if (units[ui].alive && units[ui].owner == AI_PLAYER &&
                units[ui].type == UNIT_VILLAGER && units[ui].buildTarget == bi) {
                hasBuilder = true;
                break;
            }
        }
        if (!hasBuilder) {
            int vil = unit_find_idle_villager(AI_PLAYER, 0);
            if (vil >= 0) {
                unit_command_build(vil, bi, terrain);
            }
        }
    }

    // House if near pop cap
    if (p.popCount >= p.popCap - 2) {
        int bx, by;
        if (ai_find_build_spot(tcTX, tcTY, 1, 1, terrain, bx, by)) {
            ai_place_and_build(BLDG_HOUSE, bx, by, gs, terrain);
        }
    }

    // Barracks (rush builds 2, others build 1)
    int maxBarracks = (aiStrategy == AI_STRAT_RUSH) ? 2 : 1;
    if (building_count(AI_PLAYER, BLDG_BARRACKS) < maxBarracks) {
        int bx, by;
        if (ai_find_build_spot(tcTX + 2, tcTY, 2, 2, terrain, bx, by)) {
            ai_place_and_build(BLDG_BARRACKS, bx, by, gs, terrain);
        }
    }

    // Farms if low on food (scale with age and strategy)
    int farmTarget = 3 + p.age * 2 + STRAT_FARM_MOD[aiStrategy];
    if (p.resources[RES_FOOD] < 150 && building_count(AI_PLAYER, BLDG_FARM) < farmTarget) {
        int bx, by;
        if (ai_find_build_spot(tcTX - 1, tcTY + 2, 1, 1, terrain, bx, by)) {
            ai_place_and_build(BLDG_FARM, bx, by, gs, terrain);
        }
    }

    // Lumber camp near forest if none
    if (building_count(AI_PLAYER, BLDG_LUMBER_CAMP) == 0) {
        int fx, fy;
        if (ai_find_resource(tcTX, tcTY, TERRAIN_FOREST, terrain, fx, fy)) {
            int bx, by;
            if (ai_find_build_spot(fx, fy, 1, 1, terrain, bx, by)) {
                ai_place_and_build(BLDG_LUMBER_CAMP, bx, by, gs, terrain);
            }
        }
    }

    // Mining camp near gold if none
    if (building_count(AI_PLAYER, BLDG_MINING_CAMP) == 0) {
        int gx, gy;
        if (ai_find_resource(tcTX, tcTY, TERRAIN_GOLD, terrain, gx, gy)) {
            int bx, by;
            if (ai_find_build_spot(gx, gy, 1, 1, terrain, bx, by)) {
                ai_place_and_build(BLDG_MINING_CAMP, bx, by, gs, terrain);
            }
        }
    }

    // Archery range if feudal and none
    if (p.age >= AGE_FEUDAL && building_count(AI_PLAYER, BLDG_ARCHERY_RANGE) == 0) {
        int bx, by;
        if (ai_find_build_spot(tcTX, tcTY + 3, 2, 2, terrain, bx, by)) {
            ai_place_and_build(BLDG_ARCHERY_RANGE, bx, by, gs, terrain);
        }
    }

    // Turtle strategy: build extra tower
    int maxTowers = (aiStrategy == AI_STRAT_TURTLE) ? 3 : 1;

    // Tower near TC if feudal (not on easy)
    if (diff >= AI_NORMAL && p.age >= AGE_FEUDAL && building_count(AI_PLAYER, BLDG_TOWER) < maxTowers &&
        p.resources[RES_STONE] >= 50) {
        int bx, by;
        if (ai_find_build_spot(tcTX - 1, tcTY - 1, 1, 1, terrain, bx, by)) {
            ai_place_and_build(BLDG_TOWER, bx, by, gs, terrain);
        }
    }

    // Market if feudal and none
    if (p.age >= AGE_FEUDAL && building_count(AI_PLAYER, BLDG_MARKET) == 0) {
        int bx, by;
        if (ai_find_build_spot(tcTX + 3, tcTY + 3, 2, 2, terrain, bx, by)) {
            ai_place_and_build(BLDG_MARKET, bx, by, gs, terrain);
        }
    }

    // Stable if castle and none
    if (p.age >= AGE_CASTLE && building_count(AI_PLAYER, BLDG_STABLE) == 0) {
        int bx, by;
        if (ai_find_build_spot(tcTX - 3, tcTY, 2, 2, terrain, bx, by)) {
            ai_place_and_build(BLDG_STABLE, bx, by, gs, terrain);
        }
    }

    // Castle if imperial and none, and have stone (not on easy)
    if (diff >= AI_NORMAL && p.age >= AGE_IMPERIAL && building_count(AI_PLAYER, BLDG_CASTLE) == 0 &&
        p.resources[RES_STONE] >= 650) {
        int bx, by;
        if (ai_find_build_spot(tcTX + 2, tcTY - 2, 3, 3, terrain, bx, by)) {
            ai_place_and_build(BLDG_CASTLE, bx, by, gs, terrain);
        }
    }

    // ---- Military phase ----

    // Train military units from available buildings
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive || buildings[i].owner != AI_PLAYER) continue;
        if (!building_is_complete(i)) continue;
        if (buildings[i].trainQueue[0] >= 0) continue; // already training

        switch (buildings[i].type) {
        case BLDG_BARRACKS:
            if (p.age >= AGE_FEUDAL) {
                building_train(i, UNIT_SPEARMAN, gs);
            } else {
                building_train(i, UNIT_MILITIA, gs);
            }
            break;
        case BLDG_ARCHERY_RANGE:
            building_train(i, UNIT_ARCHER, gs);
            break;
        case BLDG_STABLE:
            building_train(i, UNIT_KNIGHT, gs);
            break;
        default:
            break;
        }
    }

    // ---- Age up ----
    if (p.ageProgress < 0 && p.age < AGE_IMPERIAL) {
        int nextAge = p.age + 1;
        if (game_can_afford(gs, AI_PLAYER, AGE_COST[nextAge])) {
            game_deduct_cost(gs, AI_PLAYER, AGE_COST[nextAge]);
            p.ageProgress = 0;
        }
    }

    // ---- Attack decision ----
    int armyThreshold = AI_ARMY_BASE[diff] + p.age * AI_ARMY_PER_AGE[diff] + STRAT_ARMY_MOD[aiStrategy];
    if (armyThreshold < 1) armyThreshold = 1;

    // Retreat: if army too small and under pressure, pull back to TC
    if (militaryCount > 0 && militaryCount < 2) {
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!units[i].alive || units[i].owner != AI_PLAYER) continue;
            if (units[i].type == UNIT_VILLAGER) continue;
            if (units[i].state == USTATE_IDLE) {
                unit_command_move(i, tcTX * TILE_PX, tcTY * TILE_PX, terrain);
            }
        }
    }
    // Attack: send army to nearest enemy building
    else if (militaryCount >= armyThreshold) {
        int enemyBldg = building_nearest(0, -1, tcTX * TILE_PX, tcTY * TILE_PX);
        if (enemyBldg >= 0) {
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!units[i].alive || units[i].owner != AI_PLAYER) continue;
                if (units[i].type == UNIT_VILLAGER) continue;
                if (units[i].state == USTATE_IDLE) {
                    // Check for nearby enemy units first
                    int enemy = unit_find_nearest_enemy(i);
                    if (enemy >= 0) {
                        unit_command_attack(i, enemy);
                    } else {
                        // Attack the building directly
                        unit_command_attack_building(i, enemyBldg);
                    }
                }
            }
        }
    }
    // In between: idle military should still engage nearby enemies
    else if (militaryCount >= 2) {
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!units[i].alive || units[i].owner != AI_PLAYER) continue;
            if (units[i].type == UNIT_VILLAGER) continue;
            if (units[i].state == USTATE_IDLE) {
                int enemy = unit_find_nearest_enemy(i);
                if (enemy >= 0) {
                    unit_command_attack(i, enemy);
                }
            }
        }
    }

    // ---- Tech research ----
    // Economy techs (prioritize early)
    if (!tech_is_researched(gs, AI_PLAYER, TECH_LOOM)) {
        tech_start_research(gs, AI_PLAYER, TECH_LOOM);
    }
    if (p.age >= AGE_FEUDAL && !tech_is_researched(gs, AI_PLAYER, TECH_DOUBLE_BIT)) {
        tech_start_research(gs, AI_PLAYER, TECH_DOUBLE_BIT);
    }
    if (p.age >= AGE_FEUDAL && !tech_is_researched(gs, AI_PLAYER, TECH_WHEELBARROW)) {
        tech_start_research(gs, AI_PLAYER, TECH_WHEELBARROW);
    }
    if (p.age >= AGE_FEUDAL && !tech_is_researched(gs, AI_PLAYER, TECH_GOLD_MINING)) {
        tech_start_research(gs, AI_PLAYER, TECH_GOLD_MINING);
    }
    if (p.age >= AGE_FEUDAL && !tech_is_researched(gs, AI_PLAYER, TECH_STONE_MINING)) {
        tech_start_research(gs, AI_PLAYER, TECH_STONE_MINING);
    }
    // Castle age economy techs
    if (p.age >= AGE_CASTLE && !tech_is_researched(gs, AI_PLAYER, TECH_BOW_SAW)) {
        tech_start_research(gs, AI_PLAYER, TECH_BOW_SAW);
    }
    if (p.age >= AGE_CASTLE && !tech_is_researched(gs, AI_PLAYER, TECH_HAND_CART)) {
        tech_start_research(gs, AI_PLAYER, TECH_HAND_CART);
    }
    if (p.age >= AGE_CASTLE && !tech_is_researched(gs, AI_PLAYER, TECH_HORSE_COLLAR)) {
        tech_start_research(gs, AI_PLAYER, TECH_HORSE_COLLAR);
    }
    // Military techs
    if (p.age >= AGE_FEUDAL && !tech_is_researched(gs, AI_PLAYER, TECH_MAN_AT_ARMS)) {
        tech_start_research(gs, AI_PLAYER, TECH_MAN_AT_ARMS);
    }
    if (p.age >= AGE_CASTLE && !tech_is_researched(gs, AI_PLAYER, TECH_CROSSBOW)) {
        tech_start_research(gs, AI_PLAYER, TECH_CROSSBOW);
    }
    if (p.age >= AGE_CASTLE && !tech_is_researched(gs, AI_PLAYER, TECH_BODKIN_ARROW)) {
        tech_start_research(gs, AI_PLAYER, TECH_BODKIN_ARROW);
    }
    if (p.age >= AGE_CASTLE && !tech_is_researched(gs, AI_PLAYER, TECH_PIKE)) {
        tech_start_research(gs, AI_PLAYER, TECH_PIKE);
    }
    if (p.age >= AGE_IMPERIAL && !tech_is_researched(gs, AI_PLAYER, TECH_CAVALIER)) {
        tech_start_research(gs, AI_PLAYER, TECH_CAVALIER);
    }
    if (p.age >= AGE_IMPERIAL && !tech_is_researched(gs, AI_PLAYER, TECH_BLAST_FURNACE)) {
        tech_start_research(gs, AI_PLAYER, TECH_BLAST_FURNACE);
    }
}
