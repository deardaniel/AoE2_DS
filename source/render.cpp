#include "render.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "iso.h"
#include "fog.h"
#include "tech.h"
#include "font.h"
#include "sprite_geom.h"
#include <string.h>

// Access tech-modified stats for HP bar max values
extern UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];

// ---------------------------------------------------------------------------
// Binary sprite data (linked from data/ via bin2o)
// Each _bin is a pointer to raw indexed pixel data, _bin_size is its length
// Index 0 = transparent, 1-255 = palette color
// ---------------------------------------------------------------------------

// Palette and remap table
extern const u8 sprite_pal_bin[];
extern const u32 sprite_pal_bin_size;
extern const u8 sprite_remap_bin[];
extern const u32 sprite_remap_bin_size;

// Unit sprite sheets; each one's cell size and layout is GEOM_<name> in
// sprite_geom.h
extern const u8 spr_villager_bin[];
extern const u8 spr_villager_walk_bin[];
extern const u8 spr_villager_attack_bin[];
extern const u8 spr_villager_carry_bin[];
extern const u8 spr_lumberjack_bin[];
extern const u8 spr_lumberjack_walk_bin[];
extern const u8 spr_lumberjack_work_bin[];
extern const u8 spr_lumberjack_carry_bin[];
extern const u8 spr_miner_bin[];
extern const u8 spr_miner_walk_bin[];
extern const u8 spr_miner_work_bin[];
extern const u8 spr_miner_carry_bin[];
extern const u8 spr_builder_bin[];
extern const u8 spr_builder_walk_bin[];
extern const u8 spr_builder_work_bin[];
extern const u8 spr_farmer_bin[];
extern const u8 spr_farmer_walk_bin[];
extern const u8 spr_farmer_work_bin[];
extern const u8 spr_farmer_carry_bin[];
extern const u8 spr_forager_carry_bin[];
extern const u8 spr_militia_bin[];
extern const u8 spr_militia_walk_bin[];
extern const u8 spr_militia_fight_bin[];
extern const u8 spr_militia_die_bin[];
extern const u8 spr_archer_bin[];
extern const u8 spr_archer_walk_bin[];
extern const u8 spr_archer_fire_bin[];
extern const u8 spr_archer_die_bin[];
extern const u8 spr_knight_bin[];
extern const u8 spr_knight_walk_bin[];
extern const u8 spr_knight_fight_bin[];
extern const u8 spr_knight_die_bin[];
extern const u8 spr_spearman_bin[];
extern const u8 spr_spearman_walk_bin[];
extern const u8 spr_spearman_fight_bin[];
extern const u8 spr_spearman_die_bin[];
extern const u8 spr_scout_bin[];
extern const u8 spr_scout_walk_bin[];
extern const u8 spr_scout_fight_bin[];
extern const u8 spr_scout_die_bin[];
extern const u8 spr_sheep_stand_bin[];
extern const u8 spr_sheep_walk_bin[];
extern const u8 spr_sheep_die_bin[];
extern const u8 spr_villager_die_bin[];
extern const u8 spr_ram_stand_bin[];
extern const u8 spr_ram_walk_bin[];
extern const u8 spr_ram_fight_bin[];
extern const u8 spr_ram_die_bin[];
extern const u8 spr_mango_stand_bin[];
extern const u8 spr_mango_walk_bin[];
extern const u8 spr_mango_fight_bin[];
extern const u8 spr_mango_die_bin[];
extern const u8 spr_monk_stand_bin[];
extern const u8 spr_monk_walk_bin[];
extern const u8 spr_monk_fight_bin[];
extern const u8 spr_monk_die_bin[];

// Building sprites (32x32 or 16x16, single frame)
extern const u8 spr_town_center_bin[];
extern const u8 spr_house_bin[];
extern const u8 spr_barracks_bin[];
extern const u8 spr_archery_range_bin[];
extern const u8 spr_stable_bin[];
extern const u8 spr_mining_camp_bin[];
extern const u8 spr_lumber_camp_bin[];
extern const u8 spr_wall_bin[];
extern const u8 spr_tower_bin[];
extern const u8 spr_market_bin[];
extern const u8 spr_castle_bin[];
extern const u8 spr_monastery_bin[];
extern const u8 spr_university_bin[];
extern const u8 spr_res_tree_bin[];
extern const u8 spr_res_gold_bin[];
extern const u8 spr_res_stone_bin[];
extern const u8 spr_res_berries_bin[];
extern const u8 spr_construction_1_bin[];
extern const u8 spr_construction_2_bin[];
extern const u8 spr_construction_3_bin[];
extern const u8 spr_construction_4_bin[];

// Fire overlay sprite (16x16, 4 animation frames stacked = 1024 bytes)
extern const u8 spr_fire_bin[];

// Build menu icon sprites (32x32 indexed, single frame)
extern const u8 icon_tc_bin[];
extern const u8 icon_house_bin[];
extern const u8 icon_barracks_bin[];
extern const u8 icon_archery_range_bin[];
extern const u8 icon_stable_bin[];
extern const u8 icon_farm_bin[];
extern const u8 icon_mining_camp_bin[];
extern const u8 icon_lumber_camp_bin[];
extern const u8 icon_wall_bin[];
extern const u8 icon_tower_bin[];
extern const u8 icon_market_bin[];
extern const u8 icon_castle_bin[];
extern const u8 icon_monastery_bin[];
extern const u8 icon_university_bin[];
extern const u8 icon_unit_villager_bin[];
extern const u8 icon_unit_militia_bin[];
extern const u8 icon_unit_archer_bin[];
extern const u8 icon_unit_knight_bin[];
extern const u8 icon_unit_spearman_bin[];
extern const u8 icon_unit_scout_bin[];
extern const u8 icon_unit_ram_bin[];
extern const u8 icon_unit_mangonel_bin[];
extern const u8 icon_unit_monk_bin[];
extern const u8 icon_tech_man_at_arms_bin[];
extern const u8 icon_tech_crossbow_bin[];
extern const u8 icon_tech_cavalier_bin[];
extern const u8 icon_tech_loom_bin[];
extern const u8 icon_tech_double_bit_bin[];
extern const u8 icon_tech_wheelbarrow_bin[];
extern const u8 icon_tech_gold_mining_bin[];
extern const u8 icon_tech_stone_mining_bin[];
extern const u8 icon_tech_bow_saw_bin[];
extern const u8 icon_tech_hand_cart_bin[];
extern const u8 icon_tech_horse_collar_bin[];
extern const u8 icon_tech_blast_furnace_bin[];
extern const u8 icon_tech_bodkin_arrow_bin[];
extern const u8 icon_tech_pikeman_bin[];
extern const u8 icon_tech_ballistics_bin[];
extern const u8 icon_tech_masonry_bin[];
extern const u8 icon_tech_chemistry_bin[];
extern const u8 icon_age_feudal_bin[];
extern const u8 icon_age_castle_bin[];
extern const u8 icon_age_imperial_bin[];

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------


// Build menu icon lookup (indexed by BLDG_* enum)
static const u8* buildingIcon[BLDG_TYPE_COUNT] = {
    icon_tc_bin,            // BLDG_TOWN_CENTER
    icon_house_bin,         // BLDG_HOUSE
    icon_barracks_bin,      // BLDG_BARRACKS
    icon_archery_range_bin, // BLDG_ARCHERY_RANGE
    icon_stable_bin,        // BLDG_STABLE
    icon_farm_bin,          // BLDG_FARM
    icon_mining_camp_bin,   // BLDG_MINING_CAMP
    icon_lumber_camp_bin,   // BLDG_LUMBER_CAMP
    icon_wall_bin,          // BLDG_WALL
    icon_tower_bin,         // BLDG_TOWER
    icon_market_bin,        // BLDG_MARKET
    icon_castle_bin,        // BLDG_CASTLE
    icon_monastery_bin,     // BLDG_MONASTERY
    icon_university_bin,    // BLDG_UNIVERSITY
};

// Train menu icons (indexed by UnitTypeId; sheep are not trained)
static const u8* unitIcon[UNIT_TYPE_COUNT] = {
    icon_unit_villager_bin, icon_unit_militia_bin, icon_unit_archer_bin,
    icon_unit_knight_bin, icon_unit_spearman_bin, icon_unit_scout_bin,
    NULL,
    icon_unit_ram_bin, icon_unit_mangonel_bin, icon_unit_monk_bin,
};

// Technology icons (indexed by TechId) and age icons (indexed by age)
static const u8* techIcon[TECH_COUNT] = {
    icon_tech_man_at_arms_bin, icon_tech_crossbow_bin, icon_tech_cavalier_bin,
    icon_tech_loom_bin, icon_tech_double_bit_bin, icon_tech_wheelbarrow_bin,
    icon_tech_gold_mining_bin, icon_tech_stone_mining_bin, icon_tech_bow_saw_bin,
    icon_tech_hand_cart_bin, icon_tech_horse_collar_bin, icon_tech_blast_furnace_bin,
    icon_tech_bodkin_arrow_bin, icon_tech_pikeman_bin, icon_tech_ballistics_bin,
    icon_tech_masonry_bin, icon_tech_chemistry_bin,
};
static const u8* ageIcon[AGE_COUNT] = {
    NULL, icon_age_feudal_bin, icon_age_castle_bin, icon_age_imperial_bin,
};

// ---------------------------------------------------------------------------
// Sprite sheet tables — map unit type + state to a sheet
// ---------------------------------------------------------------------------
// A unit sheet: pixel data plus its layout (generated into sprite_geom.h)
struct UnitSheet { const u8* data; const SheetGeom* g; };
#define SHEET(name) UnitSheet{ spr_##name##_bin, &GEOM_##name }

static UnitSheet unitStandSheet[UNIT_TYPE_COUNT];
static UnitSheet unitWalkSheet[UNIT_TYPE_COUNT];
static UnitSheet unitFightSheet[UNIT_TYPE_COUNT];
static UnitSheet unitDeathSheet[UNIT_TYPE_COUNT];

// Villager role-specific sheets, indexed by VillagerRole
static UnitSheet villagerRoleStand[VROLE_COUNT];
static UnitSheet villagerRoleWalk[VROLE_COUNT];
static UnitSheet villagerRoleWork[VROLE_COUNT];  // chop/mine/build/farm
static UnitSheet villagerRoleCarry[VROLE_COUNT]; // walking with a load

// Building sheets indexed by BuildingTypeId; sizes and anchors are BLDG_GEOM
static const u8* buildingSheet[BLDG_TYPE_COUNT];
// Construction site sheets indexed by footprint size - 1 (CONSTRUCTION_GEOM)
static const u8* const constructionSheet[4] = {
    spr_construction_1_bin, spr_construction_2_bin, spr_construction_3_bin, spr_construction_4_bin,
};

// Row spans: for big sprites, the first and one-past-last non-empty pixel of
// each row, so the blit skips the empty margins (about half of a tree's cell).
// Filled in at init; two bytes per row.
static u8 spanPool[8192];
static int spanUsed = 0;
static const u8* buildingSpans[BLDG_TYPE_COUNT];
static const u8* resourceSpans[4];   // tree, gold, stone, berries: [variant][row]

static const u8* make_spans(const u8* src, int stride, int w, int h) {
    if (spanUsed + h * 2 > (int)sizeof(spanPool)) return NULL;
    u8* out = &spanPool[spanUsed];
    for (int y = 0; y < h; y++) {
        const u8* row = src + y * stride;
        int x0 = 0, x1 = w;
        while (x0 < w && row[x0] == 0) x0++;
        while (x1 > x0 && row[x1 - 1] == 0) x1--;
        out[y * 2] = x0;
        out[y * 2 + 1] = x1;
    }
    spanUsed += h * 2;
    return out;
}

static const u8* resource_sheet(u8 ttype, const ResGeom*& g, int& kind);

// Building sprite pixels with this index are ground shadow: instead of being
// drawn they darken whatever is already in the framebuffer (PAL_SHADOW in
// scripts/shared_constants.py).
static const u8 SPR_SHADOW = 1;
static u8 shadowLut[256];

// ---------------------------------------------------------------------------
// Direction to sprite frame mapping
// AoE2 SLP sprites have 5 direction groups: S(0), SW(1), W(2), NW(3), N(4)
// East-facing directions are horizontal mirrors of their west-facing counterparts.
//
// Tile directions are rotated 45° CW from screen directions in iso projection:
//   DIR_N(0)  = screen NE -> SLP NW(3) + hFlip
//   DIR_NE(1) = screen E  -> SLP W (2) + hFlip
//   DIR_E(2)  = screen SE -> SLP SW(1) + hFlip
//   DIR_SE(3) = screen S  -> SLP S (0)           (toward camera)
//   DIR_S(4)  = screen SW -> SLP SW(1)
//   DIR_SW(5) = screen W  -> SLP W (2)
//   DIR_W(6)  = screen NW -> SLP NW(3)
//   DIR_NW(7) = screen N  -> SLP N (4)           (away from camera)
// ---------------------------------------------------------------------------
static const bool DIR_HFLIP[DIR_COUNT] = { true, true, true, false, false, false, false, false };

// Sheet row (SLP direction 0-4: S, SW, W, NW, N) for each tile direction
static const int DIR_TO_SLP_DIR[DIR_COUNT] = { 3, 2, 1, 0, 1, 2, 3, 4 };

// ---------------------------------------------------------------------------
// Get the appropriate sprite sheet for a unit based on its state and role
// ---------------------------------------------------------------------------
static const UnitSheet& get_unit_sheet(const Unit& u) {
    if (u.state == USTATE_DEAD) return unitDeathSheet[u.type];

    // Villager role-specific sheets
    if (u.type == UNIT_VILLAGER) {
        int role = u.role;
        if (role >= VROLE_COUNT) role = VROLE_BASE;
        switch (u.state) {
        case USTATE_MOVING:
            // Show carry sprite when walking with resources (returning to drop-off)
            if (u.carryAmount > 0 && villagerRoleCarry[role].data)
                return villagerRoleCarry[role];
            return villagerRoleWalk[role];
        case USTATE_RETURNING:
            if (villagerRoleCarry[role].data) return villagerRoleCarry[role];
            return villagerRoleWalk[role];
        case USTATE_GATHERING:
        case USTATE_BUILDING:
            return villagerRoleWork[role];
        case USTATE_ATTACKING:
            return unitFightSheet[UNIT_VILLAGER];
        default:
            return villagerRoleStand[role];
        }
    }

    switch (u.state) {
    case USTATE_MOVING:
    case USTATE_RETURNING:
    case USTATE_SCOUTING:
        return unitWalkSheet[u.type];
    case USTATE_ATTACKING:
    case USTATE_GATHERING:
        return unitFightSheet[u.type];
    default:
        return unitStandSheet[u.type];
    }
}

// ---------------------------------------------------------------------------
// Initialize HD sprite rendering
// ---------------------------------------------------------------------------
void render_init() {
    // Load HD palette into OAM sprite palette
    dmaCopy(sprite_pal_bin, SPRITE_PALETTE_SUB, 512);

    // Reserve palette index 255 as death tint color (dark gray)
    SPRITE_PALETTE_SUB[255] = RGB15(4, 4, 4);

    // Set up unit sheet lookup tables
    unitStandSheet[UNIT_VILLAGER]   = SHEET(villager);
    unitStandSheet[UNIT_MILITIA]    = SHEET(militia);
    unitStandSheet[UNIT_ARCHER]     = SHEET(archer);
    unitStandSheet[UNIT_KNIGHT]     = SHEET(knight);
    unitStandSheet[UNIT_SPEARMAN]   = SHEET(spearman);
    unitStandSheet[UNIT_SCOUT]      = SHEET(scout);
    unitStandSheet[UNIT_SHEEP]      = SHEET(sheep_stand);
    unitStandSheet[UNIT_RAM]        = SHEET(ram_stand);
    unitStandSheet[UNIT_MANGONEL]   = SHEET(mango_stand);
    unitStandSheet[UNIT_MONK]       = SHEET(monk_stand);

    unitWalkSheet[UNIT_VILLAGER]    = SHEET(villager_walk);
    unitWalkSheet[UNIT_MILITIA]     = SHEET(militia_walk);
    unitWalkSheet[UNIT_ARCHER]      = SHEET(archer_walk);
    unitWalkSheet[UNIT_KNIGHT]      = SHEET(knight_walk);
    unitWalkSheet[UNIT_SPEARMAN]    = SHEET(spearman_walk);
    unitWalkSheet[UNIT_SCOUT]       = SHEET(scout_walk);
    unitWalkSheet[UNIT_SHEEP]       = SHEET(sheep_walk);
    unitWalkSheet[UNIT_RAM]         = SHEET(ram_walk);
    unitWalkSheet[UNIT_MANGONEL]    = SHEET(mango_walk);
    unitWalkSheet[UNIT_MONK]        = SHEET(monk_walk);

    unitFightSheet[UNIT_VILLAGER]   = SHEET(villager_attack);
    unitFightSheet[UNIT_MILITIA]    = SHEET(militia_fight);
    unitFightSheet[UNIT_ARCHER]     = SHEET(archer_fire);
    unitFightSheet[UNIT_KNIGHT]     = SHEET(knight_fight);
    unitFightSheet[UNIT_SPEARMAN]   = SHEET(spearman_fight);
    unitFightSheet[UNIT_SCOUT]      = SHEET(scout_fight);
    unitFightSheet[UNIT_SHEEP]      = SHEET(sheep_stand); // sheep don't fight
    unitFightSheet[UNIT_RAM]        = SHEET(ram_fight);
    unitFightSheet[UNIT_MANGONEL]   = SHEET(mango_fight);
    unitFightSheet[UNIT_MONK]       = SHEET(monk_fight);

    unitDeathSheet[UNIT_VILLAGER]   = SHEET(villager_die);
    unitDeathSheet[UNIT_MILITIA]    = SHEET(militia_die);
    unitDeathSheet[UNIT_ARCHER]     = SHEET(archer_die);
    unitDeathSheet[UNIT_KNIGHT]     = SHEET(knight_die);
    unitDeathSheet[UNIT_SPEARMAN]   = SHEET(spearman_die);
    unitDeathSheet[UNIT_SCOUT]      = SHEET(scout_die);
    unitDeathSheet[UNIT_SHEEP]      = SHEET(sheep_die);
    unitDeathSheet[UNIT_RAM]        = SHEET(ram_die);
    unitDeathSheet[UNIT_MANGONEL]   = SHEET(mango_die);
    unitDeathSheet[UNIT_MONK]       = SHEET(monk_die);

    // Villager role-specific sheets
    villagerRoleStand[VROLE_BASE]        = SHEET(villager);
    villagerRoleWalk[VROLE_BASE]         = SHEET(villager_walk);
    villagerRoleWork[VROLE_BASE]         = SHEET(villager_attack);
    villagerRoleCarry[VROLE_BASE]        = SHEET(villager_carry);

    villagerRoleStand[VROLE_LUMBERJACK]  = SHEET(lumberjack);
    villagerRoleWalk[VROLE_LUMBERJACK]   = SHEET(lumberjack_walk);
    villagerRoleWork[VROLE_LUMBERJACK]   = SHEET(lumberjack_work);
    villagerRoleCarry[VROLE_LUMBERJACK]  = SHEET(lumberjack_carry);

    villagerRoleStand[VROLE_MINER]       = SHEET(miner);
    villagerRoleWalk[VROLE_MINER]        = SHEET(miner_walk);
    villagerRoleWork[VROLE_MINER]        = SHEET(miner_work);
    villagerRoleCarry[VROLE_MINER]       = SHEET(miner_carry);

    villagerRoleStand[VROLE_BUILDER]     = SHEET(builder);
    villagerRoleWalk[VROLE_BUILDER]      = SHEET(builder_walk);
    villagerRoleWork[VROLE_BUILDER]      = SHEET(builder_work);
    villagerRoleCarry[VROLE_BUILDER]     = SHEET(villager_carry);     // builders use base carry

    villagerRoleStand[VROLE_FARMER]      = SHEET(farmer);
    villagerRoleWalk[VROLE_FARMER]       = SHEET(farmer_walk);
    villagerRoleWork[VROLE_FARMER]       = SHEET(farmer_work);
    villagerRoleCarry[VROLE_FARMER]      = SHEET(farmer_carry);

    villagerRoleStand[VROLE_FORAGER]     = SHEET(farmer);
    villagerRoleWalk[VROLE_FORAGER]      = SHEET(farmer_walk);
    villagerRoleWork[VROLE_FORAGER]      = SHEET(farmer_work);
    villagerRoleCarry[VROLE_FORAGER]     = SHEET(forager_carry);

    // Set up building sheet lookup tables
    buildingSheet[BLDG_TOWN_CENTER]   = spr_town_center_bin;
    buildingSheet[BLDG_HOUSE]         = spr_house_bin;
    buildingSheet[BLDG_BARRACKS]      = spr_barracks_bin;
    buildingSheet[BLDG_ARCHERY_RANGE] = spr_archery_range_bin;
    buildingSheet[BLDG_STABLE]        = spr_stable_bin;
    buildingSheet[BLDG_FARM]          = NULL;  // farm rendered as terrain
    buildingSheet[BLDG_MINING_CAMP]   = spr_mining_camp_bin;
    buildingSheet[BLDG_LUMBER_CAMP]   = spr_lumber_camp_bin;
    buildingSheet[BLDG_WALL]          = spr_wall_bin;
    buildingSheet[BLDG_TOWER]         = spr_tower_bin;
    buildingSheet[BLDG_MARKET]        = spr_market_bin;
    buildingSheet[BLDG_CASTLE]        = spr_castle_bin;
    buildingSheet[BLDG_MONASTERY]     = spr_monastery_bin;
    buildingSheet[BLDG_UNIVERSITY]    = spr_university_bin;

    // Shadow table: for every palette entry, the closest entry to that colour
    // at ~60% brightness. Built from the live palette so it can't go stale.
    for (int i = 0; i < 256; i++) {
        u16 c = BG_PALETTE_SUB[i];
        int r = ( c        & 31) * 5 / 8;
        int g = ((c >> 5)  & 31) * 5 / 8;
        int b = ((c >> 10) & 31) * 5 / 8;
        int best = i, bestDist = 0x7FFFFFFF;
        for (int j = 1; j < 256; j++) {
            u16 d = BG_PALETTE_SUB[j];
            int dr = ( d        & 31) - r;
            int dg = ((d >> 5)  & 31) - g;
            int db = ((d >> 10) & 31) - b;
            int dist = dr * dr * 2 + dg * dg * 4 + db * db;
            if (dist < bestDist) { bestDist = dist; best = j; }
        }
        shadowLut[i] = best;
    }

    // Row spans for the big sprites
    for (int t = 0; t < BLDG_TYPE_COUNT; t++) {
        buildingSpans[t] = buildingSheet[t]
            ? make_spans(buildingSheet[t], BLDG_GEOM[t].w, BLDG_GEOM[t].w, BLDG_GEOM[t].h) : NULL;
    }
    static const u8 RES_TERRAIN[4] = { TERRAIN_FOREST, TERRAIN_GOLD, TERRAIN_STONE, TERRAIN_BERRIES };
    for (int k = 0; k < 4; k++) {
        const ResGeom* g = &RES_GEOM_tree;
        int kind = 0;
        const u8* sheet = resource_sheet(RES_TERRAIN[k], g, kind);
        const u8* first = NULL;
        for (int v = 0; v < g->count; v++) {
            const u8* sp = make_spans(sheet + v * g->cw, g->count * g->cw, g->cw, g->ch);
            if (v == 0) first = sp;
            if (!sp) first = NULL;
        }
        resourceSpans[k] = first;
    }
}

// ---------------------------------------------------------------------------
// Building and unit placement
// ---------------------------------------------------------------------------
static bool bldg_complete(const Building& b) {
    return b.buildProgress >= BLDG_STATS[b.type].buildTime;
}

// Screen position of the centre of a building's footprint diamond.
// worldToIso gives the top-left of the origin tile's 32x16 cell.
static void bldg_centre(const Building& b, const GameState& gs, int& cx, int& cy) {
    const BuildingStats& st = BLDG_STATS[b.type];
    int isoX, isoY;
    worldToIso(b.x, b.y, isoX, isoY);
    cx = isoX - gs.camX + ISO_TILE_W / 2 + (st.tileW - st.tileH) * (ISO_TILE_W / 4);
    cy = isoY - gs.camY + (st.tileW + st.tileH) * (ISO_TILE_H / 4);
}

// The sprite a building currently shows: its construction site, or itself.
static const SpriteGeom& bldg_geom(const Building& b) {
    if (!bldg_complete(b)) return CONSTRUCTION_GEOM[BLDG_STATS[b.type].tileW - 1];
    return BLDG_GEOM[b.type];
}

// Top-left screen pixel of that sprite
static void bldg_sprite_pos(const Building& b, const GameState& gs, int& sx, int& sy) {
    const SpriteGeom& g = bldg_geom(b);
    bldg_centre(b, gs, sx, sy);
    sx -= g.ax;
    sy -= g.ay;
}

// Screen position of a unit's ground point: the centre of its tile
static void unit_ground(const Unit& u, const GameState& gs, int& gx, int& gy) {
    int isoX, isoY;
    worldToIso(u.x, u.y, isoX, isoY);
    gx = isoX - gs.camX + ISO_TILE_W / 2;
    gy = isoY - gs.camY + ISO_TILE_H / 2;
}

// Which building is under a screen pixel? A finished building is hit only
// where its sprite has a solid pixel within one pixel of the point — not its
// shadow, and not the bare ground of its footprint (the Town Center's
// forecourt). Construction sites and farms have no solid sprite to speak of,
// so their footprint counts. The front-most hit wins.
int render_pick_building(const GameState& gs, int screenX, int screenY) {
    int tileX, tileY;
    screenToTile(screenX, screenY, gs.camX, gs.camY, tileX, tileY);
    int best = -1, bestDepth = -0x7FFFFFFF;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        const BuildingStats& st = BLDG_STATS[b.type];
        bool hit = false;
        if (!bldg_complete(b) || buildingSheet[b.type] == NULL) {
            int dx = tileX - b.x / TILE_PX, dy = tileY - b.y / TILE_PX;
            hit = (dx >= 0 && dx < st.tileW && dy >= 0 && dy < st.tileH);
        } else {
            const SpriteGeom& g = BLDG_GEOM[b.type];
            int sx, sy;
            bldg_sprite_pos(b, gs, sx, sy);
            for (int oy = -1; oy <= 1 && !hit; oy++) {
                for (int ox = -1; ox <= 1 && !hit; ox++) {
                    int px = screenX - sx + ox, py = screenY - sy + oy;
                    if (px < 0 || py < 0 || px >= g.w || py >= g.h) continue;
                    u8 v = buildingSheet[b.type][py * g.w + px];
                    hit = (v != 0 && v != SPR_SHADOW);
                }
            }
        }
        if (!hit) continue;
        int isoX, isoY;
        worldToIso(b.x + st.tileW * TILE_PX, b.y + st.tileH * TILE_PX, isoX, isoY);
        if (isoY > bestDepth) { bestDepth = isoY; best = i; }
    }
    return best;
}

// Which unit is under a screen pixel? Judged by the unit's standing sprite —
// a box as wide as its cell and as tall as it stands, with a couple of pixels'
// slack for a stylus — so a unit doesn't swallow taps meant for the ground or
// a resource beside it. owner < 0 = anyone. Dead units don't count, except a
// sheep carcass that still has meat when withCarcasses is set; garrisoned
// units never do. Nearest ground point wins.
int render_pick_unit(const GameState& gs, int screenX, int screenY, int owner, bool withCarcasses) {
    int best = -1, bestDist = 0x7FFFFFFF;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit& u = units[i];
        if (!u.alive || u.state == USTATE_GARRISONED) continue;
        if (owner >= 0 && u.owner != owner) continue;
        if (u.state == USTATE_DEAD &&
            !(withCarcasses && u.type == UNIT_SHEEP && u.carryAmount > 0)) continue;
        const SheetGeom& g = *unitStandSheet[u.type].g;
        int gx, gy;
        unit_ground(u, gs, gx, gy);
        int dx = screenX - gx, dy = screenY - gy;
        int halfW = g.cw / 2 + 2;
        if (dx < -halfW || dx > halfW || dy < -(g.ay + 2) || dy > 4) continue;
        int dist = dx * dx + dy * dy;
        if (dist < bestDist) { bestDist = dist; best = i; }
    }
    return best;
}

// Generous on-screen test for a unit (its sheets' cells differ in size)
static bool unit_on_screen(int gx, int gy) {
    return gx > -48 && gx < SCREEN_W + 48 && gy > -48 && gy < SCREEN_H + 48;
}

// ---------------------------------------------------------------------------
// Draw a horizontal HP bar into the bitmap buffer
// ---------------------------------------------------------------------------
static void draw_hp_bar(u8* buf, int cx, int sy, int barW, int hp, int maxHp, bool forceShow = false) {
    if (maxHp <= 0) return;
    int filledW = (hp * barW) / maxHp;
    if (filledW < 0) filledW = 0;
    if (filledW > barW) filledW = barW;
    int x0 = cx - barW / 2;

    // Only draw if damaged or selected
    if (hp >= maxHp && !forceShow) return;

    for (int px = 0; px < barW; px++) {
        int screenX = x0 + px;
        if (screenX < 0 || screenX >= SCREEN_W) continue;
        if (sy < 0 || sy >= SCREEN_H) continue;
        u8 color = (px < filledW) ? PAL_GREEN : PAL_RED;
        buf[sy * 256 + screenX] = color;
    }
    // Black outline above and below (1px)
    for (int px = -1; px <= barW; px++) {
        int screenX = x0 + px;
        if (screenX < 0 || screenX >= SCREEN_W) continue;
        if (sy - 1 >= 0 && sy - 1 < SCREEN_H)
            buf[(sy - 1) * 256 + screenX] = PAL_BLACK;
        if (sy + 1 >= 0 && sy + 1 < SCREEN_H)
            buf[(sy + 1) * 256 + screenX] = PAL_BLACK;
    }
}

// ---------------------------------------------------------------------------
// Software blit: draw a sprite frame into the bitmap buffer
// Skips transparent pixels (index 0). Uses sprite palette indices directly.
// ---------------------------------------------------------------------------
static void blit_frame(u8* buf, const u8* frame, int fw, int fh,
                       int sx, int sy, bool hflip) {
    for (int py = 0; py < fh; py++) {
        int screenY = sy + py;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        for (int px = 0; px < fw; px++) {
            int screenX = sx + px;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            int srcX = hflip ? (fw - 1 - px) : px;
            u8 val = frame[py * fw + srcX];
            if (val == 0) continue; // transparent
            buf[screenY * 256 + screenX] = val;
        }
    }
}

// Unit blit: one cell straight out of a sheet (stride = sheet width), with
// optional horizontal flip and player colour remap.
static void blit_cell(u8* buf, const u8* src, int stride, int w, int h,
                      int sx, int sy, bool hflip, const u8* remap) {
    for (int py = 0; py < h; py++) {
        int screenY = sy + py;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        const u8* row = src + py * stride;
        u8* dst = buf + screenY * 256;
        for (int px = 0; px < w; px++) {
            int screenX = sx + px;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            u8 val = row[hflip ? (w - 1 - px) : px];
            if (val == 0) continue;
            dst[screenX] = remap ? remap[val] : val;
        }
    }
}

// Blit with ground shadow: SPR_SHADOW pixels darken the pixel underneath
// instead of replacing it. stride = width of the sheet the cell is cut from;
// spans (optional) = make_spans() output for these rows.
static void blit_shadowed(u8* buf, const u8* src, int stride, int w, int h, int sx, int sy,
                          const u8* spans = NULL) {
    int py0 = (sy < 0) ? -sy : 0;
    int py1 = (sy + h > SCREEN_H) ? SCREEN_H - sy : h;
    for (int py = py0; py < py1; py++) {
        int x0 = spans ? spans[py * 2] : 0;
        int x1 = spans ? spans[py * 2 + 1] : w;
        if (sx + x0 < 0) x0 = -sx;
        if (sx + x1 > SCREEN_W) x1 = SCREEN_W - sx;
        const u8* line = src + py * stride;
        u8* row = buf + (sy + py) * 256 + sx;
        for (int px = x0; px < x1; px++) {
            u8 val = line[px];
            if (val == 0) continue;
            row[px] = (val == SPR_SHADOW) ? shadowLut[row[px]] : val;
        }
    }
}

// Resource sheet for a terrain type (NULL if it has no sprite)
static const u8* resource_sheet(u8 ttype, const ResGeom*& g, int& kind) {
    switch (ttype) {
    case TERRAIN_FOREST:  g = &RES_GEOM_tree;    kind = 0; return spr_res_tree_bin;
    case TERRAIN_GOLD:    g = &RES_GEOM_gold;    kind = 1; return spr_res_gold_bin;
    case TERRAIN_STONE:   g = &RES_GEOM_stone;   kind = 2; return spr_res_stone_bin;
    case TERRAIN_BERRIES: g = &RES_GEOM_berries; kind = 3; return spr_res_berries_bin;
    default: return NULL;
    }
}

// ---------------------------------------------------------------------------
// Draw an ellipse outline into the bitmap buffer (Bresenham midpoint)
// ---------------------------------------------------------------------------
static void draw_ellipse_buf(u8* buf, int cx, int cy, int rx, int ry, u8 color) {
    int x = 0, y = ry;
    long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
    long tworx2 = 2 * rx2, twory2 = 2 * ry2;
    long px = 0, py = tworx2 * y;
    long p;

    #define EPLOT(cx, cy, x, y) do { \
        int pts[4][2] = {{(cx)+(x),(cy)+(y)},{(cx)-(x),(cy)+(y)},{(cx)+(x),(cy)-(y)},{(cx)-(x),(cy)-(y)}}; \
        for (int _i=0;_i<4;_i++) { \
            int _x=pts[_i][0], _y=pts[_i][1]; \
            if (_x>=0&&_x<SCREEN_W&&_y>=0&&_y<SCREEN_H) buf[_y*256+_x]=color; \
        } \
    } while(0)

    // Region 1
    p = ry2 - rx2 * ry + rx2 / 4;
    while (px < py) {
        EPLOT(cx, cy, x, y);
        x++; px += twory2;
        if (p < 0) { p += ry2 + px; }
        else { y--; py -= tworx2; p += ry2 + px - py; }
    }
    // Region 2
    p = ry2 * (x * 2 + 1) * (x * 2 + 1) / 4 + rx2 * ((long)(y - 1) * (y - 1) - ry2);
    while (y >= 0) {
        EPLOT(cx, cy, x, y);
        y--; py -= tworx2;
        if (p > 0) { p += rx2 - py; }
        else { x++; px += twory2; p += rx2 - py + px; }
    }
    #undef EPLOT
}

// ---------------------------------------------------------------------------
// Draw a Bresenham line into the bitmap buffer
// ---------------------------------------------------------------------------
static void draw_line_buf(u8* buf, int x0, int y0, int x1, int y1, u8 color) {
    int dx = x1 - x0, dy = y1 - y0;
    int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
    dx *= sx; dy *= sy;
    int err = dx - dy;
    while (true) {
        if (x0 >= 0 && x0 < SCREEN_W && y0 >= 0 && y0 < SCREEN_H) {
            buf[y0 * 256 + x0] = color;
        }
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

// ---------------------------------------------------------------------------
// Draw a diamond selection outline for a building's isometric footprint
// ---------------------------------------------------------------------------
static void draw_selection_diamond(u8* buf, int bScreenX, int bScreenY,
                                   int tileW, int tileH) {
    // 4 vertices of the footprint diamond relative to worldToIso origin
    int topX  = bScreenX + ISO_TILE_W / 2;
    int topY  = bScreenY;
    int rightX = bScreenX + (tileW - 1) * (ISO_TILE_W / 2) + ISO_TILE_W - 1;
    int rightY = bScreenY + tileW * (ISO_TILE_H / 2);
    int bottomX = bScreenX + (tileW - tileH) * (ISO_TILE_W / 2) + ISO_TILE_W / 2;
    int bottomY = bScreenY + (tileW + tileH) * (ISO_TILE_H / 2);
    int leftX  = bScreenX - (tileH - 1) * (ISO_TILE_W / 2);
    int leftY  = bScreenY + tileH * (ISO_TILE_H / 2);

    draw_line_buf(buf, topX, topY, rightX, rightY, PAL_WHITE);
    draw_line_buf(buf, rightX, rightY, bottomX, bottomY, PAL_WHITE);
    draw_line_buf(buf, bottomX, bottomY, leftX, leftY, PAL_WHITE);
    draw_line_buf(buf, leftX, leftY, topX, topY, PAL_WHITE);
}

// ---------------------------------------------------------------------------
// Scene rendering
//
// Drawing every tree and building each frame costs most of a frame, and they
// don't move. So the scene is drawn in two layers:
//
//   static layer   ground + resources + buildings, kept in staticBuf and only
//                  redrawn when the ground, a building or the selection
//                  changes
//   dynamic layer  units, drawn each frame over a copy of the static layer
//
// Depth still works: after a unit is drawn, any static sprite that stands in
// front of it and overlaps it is drawn again, clipped to the unit's rectangle.
// ---------------------------------------------------------------------------
struct StaticSprite {
    s16 sortY;          // depth: larger = nearer the camera
    s16 x, y, w, h;     // screen rectangle
    const u8* src;      // top-left pixel of the sprite in its sheet
    u16 stride;         // sheet width
    const u8* spans;    // row spans (may be NULL)
    bool remap;         // second player's colours
};

enum { MAX_STATIC = MAX_BUILDINGS + 192 };
static StaticSprite statics[MAX_STATIC];
static int staticCount = 0;
static u8 staticBuf[256 * 192] __attribute__((aligned(4)));

// The pixels a building currently shows: its construction stage or itself
static const u8* bldg_pixels(const Building& b) {
    const SpriteGeom& g = bldg_geom(b);
    if (!bldg_complete(b)) {
        int stage = b.buildProgress * CONSTRUCTION_STAGES / BLDG_STATS[b.type].buildTime;
        if (stage >= CONSTRUCTION_STAGES) stage = CONSTRUCTION_STAGES - 1;
        return constructionSheet[BLDG_STATS[b.type].tileW - 1] + stage * g.w * g.h;
    }
    return buildingSheet[b.type];  // NULL for a farm: drawn as terrain
}

// Draw one static sprite in full (with its shadow)
static void draw_static(u8* buf, const StaticSprite& sp) {
    if (!sp.remap) {
        blit_shadowed(buf, sp.src, sp.stride, sp.w, sp.h, sp.x, sp.y, sp.spans);
        return;
    }
    const u8* remap = sprite_remap_bin;
    int py0 = (sp.y < 0) ? -sp.y : 0;
    int py1 = (sp.y + sp.h > SCREEN_H) ? SCREEN_H - sp.y : sp.h;
    for (int py = py0; py < py1; py++) {
        int x0 = (sp.x < 0) ? -sp.x : 0;
        int x1 = (sp.x + sp.w > SCREEN_W) ? SCREEN_W - sp.x : sp.w;
        const u8* line = sp.src + py * sp.stride;
        u8* row = buf + (sp.y + py) * 256 + sp.x;
        for (int px = x0; px < x1; px++) {
            u8 val = line[px];
            if (val == 0) continue;
            row[px] = (val == SPR_SHADOW) ? shadowLut[row[px]] : remap[val];
        }
    }
}

// Draw the solid pixels of a static sprite again inside a clip rectangle
// (its shadow is already on the ground and must not darken it twice)
static void redraw_static(u8* buf, const StaticSprite& sp, int cx0, int cy0, int cx1, int cy1) {
    int x0 = (cx0 > sp.x) ? cx0 : sp.x;
    int y0 = (cy0 > sp.y) ? cy0 : sp.y;
    int x1 = (cx1 < sp.x + sp.w) ? cx1 : sp.x + sp.w;
    int y1 = (cy1 < sp.y + sp.h) ? cy1 : sp.y + sp.h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCREEN_W) x1 = SCREEN_W;
    if (y1 > SCREEN_H) y1 = SCREEN_H;
    if (x0 >= x1 || y0 >= y1) return;
    const u8* remap = sp.remap ? sprite_remap_bin : NULL;
    for (int y = y0; y < y1; y++) {
        const u8* line = sp.src + (y - sp.y) * sp.stride - sp.x;
        u8* row = buf + y * 256;
        for (int x = x0; x < x1; x++) {
            u8 val = line[x];
            if (val == 0 || val == SPR_SHADOW) continue;
            row[x] = remap ? remap[val] : val;
        }
    }
}

// What the static layer depends on besides the ground: every building's
// look, and what is selected (selection outlines are drawn into the layer)
static u32 static_signature(const GameState& gs) {
    u32 sig = (u32)(gs.selectedBldg + 1) * 31 + (u32)(gs.selectedTileX + 1) * 131 +
              (u32)(gs.selectedTileY + 1) * 517;
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        u32 stage = bldg_complete(b) ? CONSTRUCTION_STAGES
                  : (u32)(b.buildProgress * CONSTRUCTION_STAGES / BLDG_STATS[b.type].buildTime);
        sig = sig * 16777619u ^ ((u32)i | (u32)b.type << 8 | (u32)b.owner << 16 | stage << 20);
        sig = sig * 16777619u ^ ((u32)(u16)b.x | (u32)(u16)b.y << 16);
    }
    return sig;
}

// Rebuild the static layer over a fresh copy of the ground
static void build_static_layer(const u8* ground, const GameState& gs, const TerrainMap& terrain) {
    memcpy(staticBuf, ground, sizeof(staticBuf));
    staticCount = 0;

    // Resource tiles in view (trees, mines, bushes)
    int minTX, minTY, maxTX, maxTY, tTX, tTY;
    screenToTile(0, 0, gs.camX, gs.camY, minTX, minTY);
    maxTX = minTX; maxTY = minTY;
    static const int CORNER[3][2] = { { SCREEN_W, 0 }, { 0, SCREEN_H }, { SCREEN_W, SCREEN_H } };
    for (int c = 0; c < 3; c++) {
        screenToTile(CORNER[c][0], CORNER[c][1], gs.camX, gs.camY, tTX, tTY);
        if (tTX < minTX) minTX = tTX;
        if (tTX > maxTX) maxTX = tTX;
        if (tTY < minTY) minTY = tTY;
        if (tTY > maxTY) maxTY = tTY;
    }
    // A tree is 67px tall, so tiles a few rows below the screen still show
    minTX -= 1; minTY -= 1; maxTX += 5; maxTY += 5;

    for (int tx = minTX; tx <= maxTX; tx++) {
        for (int ty = minTY; ty <= maxTY; ty++) {
            if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
            if (staticCount >= MAX_STATIC) break;
            const ResGeom* g;
            int kind;
            const u8* sheet = resource_sheet(terrain.tileAt(tx, ty), g, kind);
            if (!sheet) continue;
            if (!fogMap.isExplored(0, tx, ty)) continue;

            int isoX, isoY;
            tileToIso(tx, ty, isoX, isoY);
            // Each tile shows one of the game's variants, fixed by its position
            u32 hash = (u32)(tx * 73856093) ^ (u32)(ty * 19349663);
            int variant = (hash >> 4) % g->count;
            StaticSprite& sp = statics[staticCount];
            sp.x = isoX - gs.camX + ISO_TILE_W / 2 - g->ax;
            sp.y = isoY - gs.camY + ISO_TILE_H / 2 - g->ay;
            sp.w = g->cw;
            sp.h = g->ch;
            if (sp.x + sp.w <= 0 || sp.x >= SCREEN_W || sp.y + sp.h <= 0 || sp.y >= SCREEN_H) continue;
            sp.sortY = isoY + ISO_TILE_H;
            sp.src = sheet + variant * g->cw;
            sp.stride = g->count * g->cw;
            sp.spans = resourceSpans[kind] ? resourceSpans[kind] + variant * g->ch * 2 : NULL;
            sp.remap = false;
            staticCount++;
        }
    }

    // Buildings
    for (int i = 0; i < MAX_BUILDINGS && staticCount < MAX_STATIC; i++) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        const u8* pixels = bldg_pixels(b);
        if (!pixels) continue;
        if (b.owner != 0 && !fogMap.isExplored(0, b.x / TILE_PX, b.y / TILE_PX)) continue;

        const SpriteGeom& g = bldg_geom(b);
        const BuildingStats& st = BLDG_STATS[b.type];
        StaticSprite& sp = statics[staticCount];
        int sx, sy;
        bldg_sprite_pos(b, gs, sx, sy);
        sp.x = sx; sp.y = sy; sp.w = g.w; sp.h = g.h;
        if (sp.x + sp.w <= 0 || sp.x >= SCREEN_W || sp.y + sp.h <= 0 || sp.y >= SCREEN_H) continue;
        // Depth: the footprint's near corner. The TC only stands on the back
        // half of its footprint (the front is open ground under its shadow),
        // so units beside its front edges must draw over it: use the centre.
        int topX, topY, cornerX, cornerY;
        worldToIso(b.x, b.y, topX, topY);
        worldToIso(b.x + st.tileW * TILE_PX, b.y + st.tileH * TILE_PX, cornerX, cornerY);
        sp.sortY = (b.type == BLDG_TOWN_CENTER) ? (topY + cornerY) / 2 : cornerY;
        sp.src = pixels;
        sp.stride = g.w;
        sp.spans = bldg_complete(b) ? buildingSpans[b.type] : NULL;
        sp.remap = (b.owner == 1);
        staticCount++;
    }

    // Back to front (insertion sort; the list is short and nearly sorted)
    for (int i = 1; i < staticCount; i++) {
        StaticSprite tmp = statics[i];
        int j = i - 1;
        while (j >= 0 && statics[j].sortY > tmp.sortY) { statics[j + 1] = statics[j]; j--; }
        statics[j + 1] = tmp;
    }

    // Selection outlines lie on the ground, under the sprites
    if (gs.selectedBldg >= 0 && gs.selectedBldg < MAX_BUILDINGS && buildings[gs.selectedBldg].alive) {
        const Building& b = buildings[gs.selectedBldg];
        int isoX, isoY;
        worldToIso(b.x, b.y, isoX, isoY);
        draw_selection_diamond(staticBuf, isoX - gs.camX, isoY - gs.camY,
                               BLDG_STATS[b.type].tileW, BLDG_STATS[b.type].tileH);
    }
    if (gs.selectedTileX >= 0 && gs.selectedTileX < MAP_TILES &&
        gs.selectedTileY >= 0 && gs.selectedTileY < MAP_TILES) {
        int isoX, isoY;
        tileToIso(gs.selectedTileX, gs.selectedTileY, isoX, isoY);
        draw_selection_diamond(staticBuf, isoX - gs.camX, isoY - gs.camY, 1, 1);
    }

    for (int i = 0; i < staticCount; i++) draw_static(staticBuf, statics[i]);
}

// ---------------------------------------------------------------------------
// Draw the scene: ground + static layer (cached), then units, fire, HP bars.
// `ground` is the terrain/fog layer and groundVersion changes whenever it was
// redrawn.
// ---------------------------------------------------------------------------
// Player colour used for the outline of a hidden unit (first player's ramp;
// the second player's goes through the remap table)
enum { OUTLINE_COLOUR = 19 };

// Water is part of the cached ground, so it can't be redrawn every frame.
// Instead a few short glints per water tile come and go on top of it: each
// tile has two, at fixed spots, lit for a third of a 1.6 s cycle that starts
// at a different moment for every tile. Only bare, visible water is touched
// (where the static layer still shows the ground).
static void draw_water_glints(u8* buf, const u8* ground, const GameState& gs, const TerrainMap& terrain) {
    u32 tick = (u32)gs.frameCount / 8;
    for (int ty = 0; ty < MAP_TILES; ty++) {
        for (int tx = 0; tx < MAP_TILES; tx++) {
            if (terrain.tiles[ty][tx] != TERRAIN_WATER || !fogMap.isVisible(0, tx, ty)) continue;
            int isoX, isoY;
            tileToIso(tx, ty, isoX, isoY);
            int sx = isoX - gs.camX, sy = isoY - gs.camY;
            if (sx + ISO_TILE_W <= 0 || sx >= SCREEN_W || sy + ISO_TILE_H <= 0 || sy >= SCREEN_H) continue;
            u32 h = (u32)tx * 73856093u ^ (u32)ty * 19349663u;
            for (int k = 0; k < 2; k++, h = h * 1664525u + 1013904223u) {
                u32 phase = (tick + (h >> 20)) % 12;
                if (phase >= 4) continue;
                // Inside the middle rows of the diamond, where it is 16+ px wide
                int gx = sx + 9 + (int)((h >> 8) % 12), gy = sy + 4 + (int)((h >> 14) % 8);
                int len = (phase == 0 || phase == 3) ? 2 : 3;
                if (gy < 0 || gy >= SCREEN_H) continue;
                for (int x = gx; x < gx + len; x++) {
                    if (x < 0 || x >= SCREEN_W) continue;
                    int idx = gy * 256 + x;
                    if (staticBuf[idx] == ground[idx]) buf[idx] = terrainShallowLut[ground[idx]];
                }
            }
        }
    }
}

void render_sprites_sw(u8* buf, const GameState& gs, const TerrainMap& terrain,
                       const u8* ground, u32 groundVersion) {
    static u32 haveGround = 0xFFFFFFFF, haveSig = 0;
    u32 sig = static_signature(gs);
    if (haveGround != groundVersion || haveSig != sig) {
        haveGround = groundVersion;
        haveSig = sig;
        build_static_layer(ground, gs, terrain);
    }
    memcpy(buf, staticBuf, sizeof(staticBuf));
    draw_water_glints(buf, ground, gs, terrain);

    // Visible units, back to front by where their feet are
    static u8 order[MAX_UNITS];
    static s16 depth[MAX_UNITS];
    int n = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        const Unit& u = units[i];
        if (!u.alive || u.state == USTATE_GARRISONED) continue;
        int gx, gy;
        unit_ground(u, gs, gx, gy);
        if (!unit_on_screen(gx, gy)) continue;
        if (u.owner != 0) {
            int tx = (u.x + TILE_PX / 2) / TILE_PX;
            int ty = (u.y + TILE_PX / 2) / TILE_PX;
            if (!fogMap.isVisible(0, tx, ty)) continue;
        }
        int feetX, feetY;
        worldToIso(u.x + TILE_PX / 2, u.y + TILE_PX, feetX, feetY);
        int j = n++;
        while (j > 0 && depth[j - 1] > feetY) { depth[j] = depth[j - 1]; order[j] = order[j - 1]; j--; }
        depth[j] = feetY;
        order[j] = i;
    }

    for (int k = 0; k < n; k++) {
        const Unit& u = units[order[k]];
        const UnitSheet& sheet = get_unit_sheet(u);
        const SheetGeom& g = *sheet.g;

        // Frame within the direction: looping, or played once over animFrame 0-9
        int f = g.once ? ((u.animFrame > 9 ? 9 : u.animFrame) * g.fpd / 10) : (u.animFrame % g.fpd);
        int frameIdx = DIR_TO_SLP_DIR[u.direction] * g.fpd + f;
        bool hflip = DIR_HFLIP[u.direction];
        int stride = g.cols * g.cw;
        const u8* src = sheet.data + (frameIdx / g.cols) * g.ch * stride + (frameIdx % g.cols) * g.cw;

        int gx, gy;
        unit_ground(u, gs, gx, gy);
        int x0 = gx - (hflip ? g.cw - g.ax : g.ax);
        int y0 = gy - g.ay;
        int x1 = x0 + g.cw, y1 = y0 + g.ch;

        // Selection ring on the ground under the unit
        if (gs.unitSelected[order[k]]) {
            draw_ellipse_buf(buf, gx, gy + 1, 9, 4, PAL_WHITE);
            if (gx - 10 < x0) x0 = gx - 10;
            if (gx + 11 > x1) x1 = gx + 11;
            if (gy + 6 > y1) y1 = gy + 6;
        }
        blit_cell(buf, src, stride, g.cw, g.ch, gx - (hflip ? g.cw - g.ax : g.ax), gy - g.ay,
                  hflip, u.owner == 1 ? sprite_remap_bin : NULL);

        // Anything static standing in front of the unit covers it again
        for (int si = staticCount - 1; si >= 0 && statics[si].sortY > depth[k]; si--) {
            const StaticSprite& sp = statics[si];
            if (sp.x >= x1 || sp.x + sp.w <= x0 || sp.y >= y1 || sp.y + sp.h <= y0) continue;
            redraw_static(buf, sp, x0, y0, x1, y1);
        }
    }

    // Fire on damaged buildings
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        const Building& b = buildings[i];
        int maxHp = BLDG_STATS[b.type].hp;
        if (!b.alive || !bldg_complete(b) || buildingSheet[b.type] == NULL || b.hp >= maxHp / 2) continue;
        if (b.owner != 0 && !fogMap.isExplored(0, b.x / TILE_PX, b.y / TILE_PX)) continue;
        const SpriteGeom& g = BLDG_GEOM[b.type];
        int sx, sy;
        bldg_sprite_pos(b, gs, sx, sy);
        const int fireW = 16, fireH = 16;
        // First fire: centered horizontally, 1/4 down from top
        const u8* fire = spr_fire_bin + ((gs.frameCount / 8) % 4) * fireW * fireH;
        blit_frame(buf, fire, fireW, fireH, sx + g.w / 2 - fireW / 2, sy + g.h / 4 - fireH / 2, false);
        // Second fire at < 25% HP: offset left, 1/3 down
        if (b.hp < maxHp / 4) {
            fire = spr_fire_bin + (((gs.frameCount + 13) / 8) % 4) * fireW * fireH;
            blit_frame(buf, fire, fireW, fireH, sx + g.w / 3 - fireW / 2, sy + g.h / 3 - fireH / 2, false);
        }
    }

    // --- Overlay pass: HP bars drawn on top of everything ---
    for (int k = 0; k < n; k++) {
        const Unit& u = units[order[k]];
        int gx, gy;
        unit_ground(u, gs, gx, gy);
        // Just above the head: the standing sheet's anchor row is its height
        draw_hp_bar(buf, gx, gy - unitStandSheet[u.type].g->ay - 4, 12,
                    u.hp, playerUnitStats[u.owner][u.type].hp, gs.unitSelected[order[k]]);
    }

    for (int i = 0; i < MAX_BUILDINGS; i++) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        const SpriteGeom& g = bldg_geom(b);
        int bsx, bsy;
        bldg_sprite_pos(b, gs, bsx, bsy);
        if (bsx < -g.w || bsx >= SCREEN_W || bsy < -g.h || bsy >= SCREEN_H) continue;
        int cx, cy;
        bldg_centre(b, gs, cx, cy);
        int hpW = (BLDG_STATS[b.type].tileW + BLDG_STATS[b.type].tileH) * 4 + 8;
        draw_hp_bar(buf, cx, bsy - 3, hpW, b.hp, BLDG_STATS[b.type].hp);
    }
}

// ---------------------------------------------------------------------------
// Menu bars
// ---------------------------------------------------------------------------
// One 32x32 menu slot. An item you can't afford is darkened; one that needs
// a later age is darker still, so the two read differently at a glance.
enum MenuItemState { MENU_ITEM_OK, MENU_ITEM_TOO_DEAR, MENU_ITEM_LOCKED };

static void draw_menu_icon(u8* vram, int slot, const u8* icon, MenuItemState state, bool selected) {
    int ix = slot * BUILD_MENU_ITEM_W;
    if (icon) {
        for (int py = 0; py < 32; py++) {
            int dy = BUILD_MENU_Y + py;
            if (dy >= SCREEN_H) break;
            for (int px = 0; px < 32; px++) {
                u8 c = icon[py * 32 + px];
                if (c == 0) continue;
                if (state != MENU_ITEM_OK) c = shadowLut[c];
                if (state == MENU_ITEM_LOCKED) c = shadowLut[shadowLut[c]];
                vram[dy * 256 + ix + px] = c;
            }
        }
    }
    // Slot frame: yellow when selected, otherwise a black divider on the right
    for (int y = BUILD_MENU_Y; y < BUILD_MENU_Y + BUILD_MENU_H && y < SCREEN_H; y++) {
        vram[y * 256 + ix + BUILD_MENU_ITEM_W - 1] = selected ? PAL_YELLOW : PAL_BLACK;
        if (selected) vram[y * 256 + ix] = PAL_YELLOW;
    }
    if (selected) {
        for (int x = ix; x < ix + BUILD_MENU_ITEM_W; x++) {
            vram[BUILD_MENU_Y * 256 + x] = PAL_YELLOW;
            if (BUILD_MENU_Y + BUILD_MENU_H - 1 < SCREEN_H)
                vram[(BUILD_MENU_Y + BUILD_MENU_H - 1) * 256 + x] = PAL_YELLOW;
        }
    }
}

static void fill_menu_bar(u8* vram) {
    for (int y = BUILD_MENU_Y; y < BUILD_MENU_Y + BUILD_MENU_H && y < SCREEN_H; y++)
        memset(&vram[y * 256], PAL_DARKBROWN, SCREEN_W);
}

void render_build_menu(u8* vram, const GameState& gs) {
    if (!gs.buildMenuOpen) return;
    fill_menu_bar(vram);

    for (int s = 0; s < BUILD_MENU_SLOTS; s++) {
        int i = gs.buildMenuPage * BUILD_MENU_SLOTS + s;
        if (i >= BLDG_TYPE_COUNT) break;
        MenuItemState state =
            (gs.players[0].age < BLDG_STATS[i].ageReq) ? MENU_ITEM_LOCKED :
            !game_can_afford(gs, 0, BLDG_STATS[i].cost) ? MENU_ITEM_TOO_DEAR : MENU_ITEM_OK;
        draw_menu_icon(vram, s, buildingIcon[i], state, false);
    }

    // Last slot: which page this is; tapping it (or X) turns the page
    if (BUILD_MENU_PAGES > 1) {
        char pageStr[4] = { (char)('1' + gs.buildMenuPage), '/', (char)('0' + BUILD_MENU_PAGES), 0 };
        int slotX = BUILD_MENU_SLOTS * BUILD_MENU_ITEM_W;
        int px = slotX + (BUILD_MENU_ITEM_W - font_string_width(gameFont, pageStr)) / 2;
        int py = BUILD_MENU_Y + (BUILD_MENU_H - gameFont.height) / 2;
        font_draw_str_8(vram, 256, SCREEN_H, px, py, pageStr, PAL_YELLOW, gameFont);
    }
}

// ---------------------------------------------------------------------------
// Building menu bar — units, technologies and the next age (building_menu_items)
// ---------------------------------------------------------------------------
void render_train_menu(u8* vram, const GameState& gs) {
    if (gs.buildMenuOpen) return; // build menu takes priority
    if (gs.selectedBldg < 0) return;
    if (gs.selectionCount > 0) return; // units selected, not building
    const Building& b = buildings[gs.selectedBldg];
    if (!b.alive || b.owner != 0 || !building_is_complete(gs.selectedBldg)) return;

    BldgMenuItem items[MENU_BAR_SLOTS];
    int count = building_menu_items(gs, gs.selectedBldg, items, MENU_BAR_SLOTS);
    if (count == 0) return;
    fill_menu_bar(vram);
    for (int slot = 0; slot < count; slot++) {
        BldgMenuItem it = items[slot];
        const char* name; const int* cost;
        bool unlocked = building_menu_item_info(gs, it, name, cost);
        MenuItemState state = !unlocked ? MENU_ITEM_LOCKED :
            !game_can_afford(gs, 0, cost) ? MENU_ITEM_TOO_DEAR : MENU_ITEM_OK;
        const u8* icon = (it.kind == MENU_UNIT) ? unitIcon[it.id] :
                         (it.kind == MENU_TECH) ? techIcon[it.id] : ageIcon[it.id];
        bool selected = (it.kind == MENU_UNIT) ? (gs.menuKind < 0 && it.id == gs.trainUnitType)
                                               : (it.kind == gs.menuKind && it.id == gs.menuId);
        draw_menu_icon(vram, slot, icon, state, selected);
    }
}

// ---------------------------------------------------------------------------
// Building placement preview — draws diamond-shaped tile highlights
// under the current touch position when in placement mode
// ---------------------------------------------------------------------------
void render_placement_preview(u8* buf, const GameState& gs, const TerrainMap& terrain) {
    if (gs.inputMode != 1) return;

    // The building follows the stylus, and stays where it was last pointed
    // while nothing is touching the screen
    const BuildingStats& st = BLDG_STATS[gs.placeBldgType];
    int baseTX, baseTY;
    placementOrigin(gs.dragEndX, gs.dragEndY, gs.camX, gs.camY, st.tileW, st.tileH, baseTX, baseTY);

    // Footprint tiles, green where the building can stand and red where not
    bool allValid = true;
    for (int dy = 0; dy < st.tileH; dy++) {
        for (int dx = 0; dx < st.tileW; dx++) {
            int tx = baseTX + dx;
            int ty = baseTY + dy;
            bool valid = (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES &&
                          terrain.canBuild(tx, ty) && building_at_tile(tx, ty) < 0);
            if (!valid) allValid = false;
            u8 color = valid ? PAL_GREEN : PAL_RED;

            int isoX, isoY;
            tileToIso(tx, ty, isoX, isoY);
            int dstX = isoX - gs.camX;
            int dstY = isoY - gs.camY;
            for (int py = 0; py < ISO_TILE_H; py++) {
                int sy = dstY + py;
                if (sy < 0 || sy >= SCREEN_H) continue;
                int xs = ISO_DIAMOND_XSTART[py];
                int xe = ISO_DIAMOND_XEND[py];
                for (int px = xs; px < xe; px++) {
                    if (px == xs || px == xe - 1 || py == 0 || py == ISO_TILE_H - 1) {
                        int sx = dstX + px;
                        if (sx >= 0 && sx < SCREEN_W) buf[sy * 256 + sx] = color;
                    }
                }
            }
        }
    }

    // A see-through copy of the building itself (every other pixel), so you
    // can judge how it sits; left out where it can't be built
    const u8* sheet = buildingSheet[gs.placeBldgType];
    if (!allValid || sheet == NULL) return;
    const SpriteGeom& g = BLDG_GEOM[gs.placeBldgType];
    int isoX, isoY;
    tileToIso(baseTX, baseTY, isoX, isoY);
    int cx = isoX - gs.camX + ISO_TILE_W / 2 + (st.tileW - st.tileH) * (ISO_TILE_W / 4);
    int cy = isoY - gs.camY + (st.tileW + st.tileH) * (ISO_TILE_H / 4);
    for (int py = 0; py < g.h; py++) {
        int sy = cy - g.ay + py;
        if (sy < 0 || sy >= SCREEN_H) continue;
        for (int px = 0; px < g.w; px++) {
            int sx = cx - g.ax + px;
            if (sx < 0 || sx >= SCREEN_W || ((sx + sy) & 1)) continue;
            u8 c = sheet[py * g.w + px];
            if (c != 0 && c != SPR_SHADOW) buf[sy * 256 + sx] = c;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw drag-selection box overlay
// ---------------------------------------------------------------------------
void render_drag_box(u8* buf, const GameState& gs) {
    if (!gs.isDragging || !gs.touchActive) return;

    int x0 = gs.dragStartX, y0 = gs.dragStartY;
    int x1 = gs.dragEndX,   y1 = gs.dragEndY;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    // Draw box outline
    for (int x = x0; x <= x1; x++) {
        if (x >= 0 && x < SCREEN_W) {
            if (y0 >= 0 && y0 < SCREEN_H) buf[y0 * 256 + x] = PAL_WHITE;
            if (y1 >= 0 && y1 < SCREEN_H) buf[y1 * 256 + x] = PAL_WHITE;
        }
    }
    for (int y = y0; y <= y1; y++) {
        if (y >= 0 && y < SCREEN_H) {
            if (x0 >= 0 && x0 < SCREEN_W) buf[y * 256 + x0] = PAL_WHITE;
            if (x1 >= 0 && x1 < SCREEN_W) buf[y * 256 + x1] = PAL_WHITE;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw move target marker — flashing yellow diamond at move destination
// ---------------------------------------------------------------------------
void render_move_target(u8* buf, GameState& gs) {
    if (gs.moveTargetTimer == 0) return;
    gs.moveTargetTimer--;

    // Flash: visible every other 4 frames
    if ((gs.moveTargetTimer / 4) & 1) return;

    int sx = gs.moveTargetIsoX - gs.camX;
    int sy = gs.moveTargetIsoY - gs.camY;

    // Draw a small 8x4 yellow diamond
    static const int DH = 4;
    for (int py = 0; py < DH; py++) {
        int half = (py < DH/2) ? (py + 1) : (DH - py);
        int cx = sx;
        int cy = sy - DH / 2 + py;
        if (cy < 0 || cy >= SCREEN_H) continue;
        for (int px = -half; px < half; px++) {
            int x = cx + px;
            if (x < 0 || x >= SCREEN_W) continue;
            if (px == -half || px == half - 1 || py == 0 || py == DH - 1)
                buf[cy * 256 + x] = PAL_YELLOW;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw training progress bars on buildings
// ---------------------------------------------------------------------------
void render_training_bars(u8* buf, const GameState& gs) {
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        const Building& b = buildings[i];
        if (!b.alive) continue;
        if (b.trainQueue[0] < 0) continue;
        if (b.buildProgress < BLDG_STATS[b.type].buildTime) continue;

        int tileW = BLDG_STATS[b.type].tileW;
        int tileH = BLDG_STATS[b.type].tileH;
        int bIsoX, bIsoY;
        worldToIso(b.x, b.y, bIsoX, bIsoY);
        int bScreenX = bIsoX - gs.camX;
        int bScreenY = bIsoY - gs.camY;

        int hpCx = bScreenX + ISO_TILE_W / 2;
        int barW = (tileW + tileH) * 8;
        int barY = bScreenY - 6; // 3px below HP bar position
        if (barY < 0 || barY >= SCREEN_H) continue;

        u8 unitType = b.trainQueue[0];
        int trainTime = UNIT_STATS[unitType].trainTime;
        int filledW = (trainTime > 0) ? (b.trainProgress * barW) / trainTime : 0;
        if (filledW > barW) filledW = barW;

        int x0 = hpCx - barW / 2;
        for (int px = 0; px < barW; px++) {
            int screenX = x0 + px;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            u8 color = (px < filledW) ? PAL_BLUE : PAL_DARKGRAY;
            buf[barY * 256 + screenX] = color;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw age advancement progress bar at top of bottom screen
// ---------------------------------------------------------------------------
void render_age_progress(u8* buf, const GameState& gs) {
    if (gs.players[0].ageProgress < 0) return;

    int nextAge = gs.players[0].age + 1;
    if (nextAge >= AGE_COUNT) return;

    int totalTime = AGE_RESEARCH_TIME[nextAge];
    if (totalTime <= 0) return;

    int filledW = (gs.players[0].ageProgress * SCREEN_W) / totalTime;
    if (filledW > SCREEN_W) filledW = SCREEN_W;

    // Draw 2px gold bar at top of screen
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            buf[y * 256 + x] = (x < filledW) ? PAL_YELLOW : PAL_DARKGRAY;
        }
    }
}
