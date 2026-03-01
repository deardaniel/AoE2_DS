#include "tech.h"
#include "game.h"
#include "buildings.h"
#include <string.h>

UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];

void tech_init_stats() {
    for (int p = 0; p < NUM_PLAYERS; p++) {
        memcpy(playerUnitStats[p], UNIT_STATS, sizeof(UNIT_STATS));
    }
}

bool tech_is_researched(const GameState& gs, int player, int techId) {
    if (player < 0 || player >= NUM_PLAYERS || techId < 0 || techId >= TECH_COUNT) return false;
    return (gs.players[player].techResearched & (1 << techId)) != 0;
}

bool tech_start_research(GameState& gs, int player, int techId) {
    if (techId < 0 || techId >= TECH_COUNT) return false;
    if (tech_is_researched(gs, player, techId)) return false;

    const TechInfo& ti = TECH_TABLE[techId];

    // Check age
    if (gs.players[player].age < ti.ageReq) return false;

    // Check cost
    if (!game_can_afford(gs, player, ti.cost)) return false;

    // Check building exists and is complete
    bool hasBldg = false;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (buildings[i].alive && buildings[i].owner == (u8)player &&
            buildings[i].type == ti.bldgReq && building_is_complete(i)) {
            hasBldg = true;
            break;
        }
    }
    if (!hasBldg) return false;

    // Deduct cost and apply immediately (simplified — no research time for techs)
    game_deduct_cost(gs, player, ti.cost);
    gs.players[player].techResearched |= (1 << techId);
    tech_apply_bonuses(player);
    return true;
}

void tech_apply_bonuses(int player) {
    // Reset to base stats
    memcpy(playerUnitStats[player], UNIT_STATS, sizeof(UNIT_STATS));

    extern GameState gameState;
    u8 researched = gameState.players[player].techResearched;

    if (researched & (1 << TECH_MAN_AT_ARMS)) {
        playerUnitStats[player][UNIT_MILITIA].hp += 2;
        playerUnitStats[player][UNIT_MILITIA].attack += 1;
    }
    if (researched & (1 << TECH_CROSSBOW)) {
        playerUnitStats[player][UNIT_ARCHER].range += 1;
        playerUnitStats[player][UNIT_ARCHER].attack += 2;
    }
    if (researched & (1 << TECH_CAVALIER)) {
        playerUnitStats[player][UNIT_KNIGHT].hp += 20;
        playerUnitStats[player][UNIT_KNIGHT].attack += 2;
    }
}
