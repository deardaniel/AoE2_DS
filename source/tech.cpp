#include "tech.h"
#include "game.h"
#include "buildings.h"
#include <string.h>

UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];
u8 playerGatherRate[NUM_PLAYERS][RES_COUNT];
u8 playerCarryMax[NUM_PLAYERS];
s16 playerFarmFood[NUM_PLAYERS];

void tech_init_stats() {
    for (int p = 0; p < NUM_PLAYERS; p++) {
        memcpy(playerUnitStats[p], UNIT_STATS, sizeof(UNIT_STATS));
        for (int r = 0; r < RES_COUNT; r++)
            playerGatherRate[p][r] = GATHER_RATE;
        playerCarryMax[p] = GATHER_CARRY_MAX;
        playerFarmFood[p] = FARM_RESOURCE_AMT;
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
    for (int r = 0; r < RES_COUNT; r++)
        playerGatherRate[player][r] = GATHER_RATE;
    playerCarryMax[player] = GATHER_CARRY_MAX;
    playerFarmFood[player] = FARM_RESOURCE_AMT;

    extern GameState gameState;
    u32 researched = gameState.players[player].techResearched;

    // Military techs
    if (researched & (1 << TECH_MAN_AT_ARMS)) {
        playerUnitStats[player][UNIT_MILITIA].hp += 2;
        playerUnitStats[player][UNIT_MILITIA].attack[DMG_MELEE] += 1;
    }
    if (researched & (1 << TECH_CROSSBOW)) {
        playerUnitStats[player][UNIT_ARCHER].range += 1;
        playerUnitStats[player][UNIT_ARCHER].attack[DMG_PIERCE] += 2;
    }
    if (researched & (1 << TECH_CAVALIER)) {
        playerUnitStats[player][UNIT_KNIGHT].hp += 20;
        playerUnitStats[player][UNIT_KNIGHT].attack[DMG_MELEE] += 2;
    }

    // Economy techs
    if (researched & (1 << TECH_LOOM)) {
        playerUnitStats[player][UNIT_VILLAGER].hp += 15;
        playerUnitStats[player][UNIT_VILLAGER].armor[DMG_MELEE] += 1;
        playerUnitStats[player][UNIT_VILLAGER].armor[DMG_PIERCE] += 1;
    }
    if (researched & (1 << TECH_WHEELBARROW)) {
        playerUnitStats[player][UNIT_VILLAGER].speed += 2;  // about +10%, as in the original
        playerCarryMax[player] += 5;
    }

    // Resource-specific gather rate bonuses (lower = faster)
    if (researched & (1 << TECH_DOUBLE_BIT)) {
        playerGatherRate[player][RES_WOOD] = GATHER_RATE * 4 / 5; // 20% faster
    }
    if (researched & (1 << TECH_BOW_SAW)) {
        // Stacks: if double-bit already applied, reduce further
        int base = (researched & (1 << TECH_DOUBLE_BIT))
                   ? (GATHER_RATE * 4 / 5) : GATHER_RATE;
        playerGatherRate[player][RES_WOOD] = base * 4 / 5; // another 20%
    }
    if (researched & (1 << TECH_GOLD_MINING)) {
        playerGatherRate[player][RES_GOLD] = GATHER_RATE * 4 / 5; // ~20% faster
    }
    if (researched & (1 << TECH_STONE_MINING)) {
        playerGatherRate[player][RES_STONE] = GATHER_RATE * 4 / 5; // ~20% faster
    }

    // Castle/Imperial techs
    if (researched & (1 << TECH_HAND_CART)) {
        playerUnitStats[player][UNIT_VILLAGER].speed += 2;  // about +10%, as in the original
        playerCarryMax[player] += 7;
    }
    if (researched & (1 << TECH_HORSE_COLLAR)) {
        playerFarmFood[player] += 75;
    }
    if (researched & (1 << TECH_BLAST_FURNACE)) {
        // +2 melee attack to all melee units
        playerUnitStats[player][UNIT_MILITIA].attack[DMG_MELEE] += 2;
        playerUnitStats[player][UNIT_KNIGHT].attack[DMG_MELEE] += 2;
        playerUnitStats[player][UNIT_SPEARMAN].attack[DMG_MELEE] += 2;
        playerUnitStats[player][UNIT_SCOUT].attack[DMG_MELEE] += 2;
    }
    if (researched & (1 << TECH_BODKIN_ARROW)) {
        playerUnitStats[player][UNIT_ARCHER].range += 1;
        playerUnitStats[player][UNIT_ARCHER].attack[DMG_PIERCE] += 1;
    }
    if (researched & (1 << TECH_PIKE)) {
        playerUnitStats[player][UNIT_SPEARMAN].hp += 30;
        playerUnitStats[player][UNIT_SPEARMAN].attack[DMG_BONUS_CAV] += 5;
    }

    // University techs
    if (researched & (1 << TECH_BALLISTICS)) {
        playerUnitStats[player][UNIT_ARCHER].range += 1;
        playerUnitStats[player][UNIT_MANGONEL].range += 1;
    }
    if (researched & (1 << TECH_CHEMISTRY)) {
        playerUnitStats[player][UNIT_ARCHER].attack[DMG_PIERCE] += 1;
        playerUnitStats[player][UNIT_MANGONEL].attack[DMG_PIERCE] += 1;
    }
    // Masonry: building HP bonus applied at research time (no per-frame effect needed)
}
