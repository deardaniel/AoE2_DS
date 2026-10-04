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
// Apply player color remap for player 2 (in-place, modifies buffer)
// ---------------------------------------------------------------------------
static void apply_color_remap(u8* buf, int size) {
    const u8* remap = sprite_remap_bin;
    for (int i = 0; i < size; i++) {
        buf[i] = remap[buf[i]];
    }
}

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
// instead of replacing it. stride = width of the sheet the cell is cut from.
static void blit_shadowed(u8* buf, const u8* src, int stride, int w, int h, int sx, int sy) {
    for (int py = 0; py < h; py++) {
        int screenY = sy + py;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        u8* row = buf + screenY * 256;
        const u8* line = src + py * stride;
        for (int px = 0; px < w; px++) {
            u8 val = line[px];
            if (val == 0) continue;
            int screenX = sx + px;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            row[screenX] = (val == SPR_SHADOW) ? shadowLut[row[screenX]] : val;
        }
    }
}

static void blit_building(u8* buf, const u8* frame, int fw, int fh, int sx, int sy) {
    blit_shadowed(buf, frame, fw, fw, fh, sx, sy);
}

// Resource sheet for a terrain type (NULL if it has no sprite)
static const u8* resource_sheet(u8 ttype, const ResGeom*& g) {
    switch (ttype) {
    case TERRAIN_FOREST:  g = &RES_GEOM_tree;    return spr_res_tree_bin;
    case TERRAIN_GOLD:    g = &RES_GEOM_gold;    return spr_res_gold_bin;
    case TERRAIN_STONE:   g = &RES_GEOM_stone;   return spr_res_stone_bin;
    case TERRAIN_BERRIES: g = &RES_GEOM_berries; return spr_res_berries_bin;
    default: return NULL;
    }
}

// ---------------------------------------------------------------------------
// Render list entry for Y-sorted drawing (back-to-front depth ordering)
// ---------------------------------------------------------------------------
struct RenderEntry {
    s16 sortY;   // bottom Y in world pixels (feet position) — lower = drawn first
    u8  kind;    // 0 = building, 1 = unit, 2 = resource tile
    u8  idx;     // index into units[] or buildings[], or packed tile coords (tx<<5|ty)
    u8  tx, ty;  // tile coords (only used for resource tiles)
};

// Simple insertion sort — fast for small N (max ~80 entries)
static void sort_render_list(RenderEntry* list, int count) {
    for (int i = 1; i < count; i++) {
        RenderEntry tmp = list[i];
        int j = i - 1;
        while (j >= 0 && list[j].sortY > tmp.sortY) {
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = tmp;
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
// Draw a single building into the bitmap buffer
// ---------------------------------------------------------------------------
static void render_building_sw(u8* buf, const GameState& gs, int i) {
    Building& b = buildings[i];
    const SpriteGeom& g = bldg_geom(b);
    int pw = g.w, ph = g.h;
    int sx, sy;
    bldg_sprite_pos(b, gs, sx, sy);

    const u8* sprData;
    if (!bldg_complete(b)) {
        // Construction site: the stages of the game's own CNSTn_NN graphic
        // for this footprint, spread evenly over the build time.
        int stage = b.buildProgress * CONSTRUCTION_STAGES / BLDG_STATS[b.type].buildTime;
        if (stage >= CONSTRUCTION_STAGES) stage = CONSTRUCTION_STAGES - 1;
        sprData = constructionSheet[BLDG_STATS[b.type].tileW - 1] + stage * pw * ph;
    } else {
        sprData = buildingSheet[b.type];
        if (sprData == NULL) return;  // farm: drawn as terrain
    }

    if (b.owner == 1) {
        static u8 frame[160 * 128];  // largest building sprite (castle with shadow, 146x117)
        memcpy(frame, sprData, pw * ph);
        apply_color_remap(frame, pw * ph);
        sprData = frame;
    }
    blit_building(buf, sprData, pw, ph, sx, sy);

    // Fire overlay on damaged buildings
    int maxHp = BLDG_STATS[b.type].hp;
    if (bldg_complete(b) && maxHp > 0 && b.hp < maxHp / 2) {
        int fireW = 16, fireH = 16;
        int fireFrame = (gs.frameCount / 8) % 4;
        const u8* fireSrc = spr_fire_bin + fireFrame * fireW * fireH;

        // First fire: centered horizontally, 1/4 down from top
        blit_frame(buf, fireSrc, fireW, fireH, sx + pw / 2 - fireW / 2, sy + ph / 4 - fireH / 2, false);

        // Second fire at < 25% HP: offset left, 1/3 down
        if (b.hp < maxHp / 4) {
            int fireFrame2 = ((gs.frameCount + 13) / 8) % 4;
            const u8* fireSrc2 = spr_fire_bin + fireFrame2 * fireW * fireH;
            blit_frame(buf, fireSrc2, fireW, fireH, sx + pw / 3 - fireW / 2, sy + ph / 3 - fireH / 2, false);
        }
    }
}

// ---------------------------------------------------------------------------
// Draw a resource sprite (tree, gold mine, stone mine) into the bitmap buffer
// ---------------------------------------------------------------------------
static void render_resource_sw(u8* buf, const GameState& gs, int tx, int ty, const TerrainMap& terrain) {
    const ResGeom* g;
    const u8* sheet = resource_sheet(terrain.tileAt(tx, ty), g);
    if (!sheet) return;

    int isoX, isoY;
    tileToIso(tx, ty, isoX, isoY);

    // Each tile shows one of the game's variants, fixed by its position
    u32 hash = (u32)(tx * 73856093) ^ (u32)(ty * 19349663);
    int variant = (hash >> 4) % g->count;
    blit_shadowed(buf, sheet + variant * g->cw, g->count * g->cw, g->cw, g->ch,
                  isoX - gs.camX + ISO_TILE_W / 2 - g->ax,
                  isoY - gs.camY + ISO_TILE_H / 2 - g->ay);

    // Selection diamond when this tile is selected
    if (gs.selectedTileX == tx && gs.selectedTileY == ty) {
        int dsx = isoX - gs.camX;
        int dsy = isoY - gs.camY;
        int hw = ISO_TILE_W / 2;
        int hh = ISO_TILE_H / 2;
        int cx = dsx + hw;
        int cy = dsy + hh;
        draw_line_buf(buf, cx, cy - hh, cx + hw, cy, PAL_WHITE);
        draw_line_buf(buf, cx + hw, cy, cx, cy + hh, PAL_WHITE);
        draw_line_buf(buf, cx, cy + hh, cx - hw, cy, PAL_WHITE);
        draw_line_buf(buf, cx - hw, cy, cx, cy - hh, PAL_WHITE);
    }
}

// ---------------------------------------------------------------------------
// Draw a single unit into the bitmap buffer
// ---------------------------------------------------------------------------
static void render_unit_sw(u8* buf, const GameState& gs, int i) {
    Unit& u = units[i];
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
    blit_cell(buf, src, stride, g.cw, g.ch, gx - (hflip ? g.cw - g.ax : g.ax), gy - g.ay,
              hflip, u.owner == 1 ? sprite_remap_bin : NULL);
}

// ---------------------------------------------------------------------------
// Software-render all visible units and buildings into bitmap buffer
// Y-sorted back-to-front so entities behind others draw first.
// ---------------------------------------------------------------------------
void render_sprites_sw(u8* buf, const GameState& gs, const TerrainMap& terrain) {
    // Build a combined render list of all visible entities
    // Max visible on screen: ~50 units + 30 buildings + ~100 resource tiles
    static RenderEntry renderList[MAX_UNITS + MAX_BUILDINGS + 128];
    int count = 0;

    // Add visible resource tiles (trees, gold mines, stone mines)
    {
        int minTX, minTY, maxTX, maxTY;
        int tmpTX, tmpTY;

        screenToTile(0, 0, gs.camX, gs.camY, minTX, minTY);
        maxTX = minTX; maxTY = minTY;

        screenToTile(SCREEN_W, 0, gs.camX, gs.camY, tmpTX, tmpTY);
        if (tmpTX < minTX) minTX = tmpTX; if (tmpTX > maxTX) maxTX = tmpTX;
        if (tmpTY < minTY) minTY = tmpTY; if (tmpTY > maxTY) maxTY = tmpTY;

        screenToTile(0, SCREEN_H, gs.camX, gs.camY, tmpTX, tmpTY);
        if (tmpTX < minTX) minTX = tmpTX; if (tmpTX > maxTX) maxTX = tmpTX;
        if (tmpTY < minTY) minTY = tmpTY; if (tmpTY > maxTY) maxTY = tmpTY;

        screenToTile(SCREEN_W, SCREEN_H, gs.camX, gs.camY, tmpTX, tmpTY);
        if (tmpTX < minTX) minTX = tmpTX; if (tmpTX > maxTX) maxTX = tmpTX;
        if (tmpTY < minTY) minTY = tmpTY; if (tmpTY > maxTY) maxTY = tmpTY;

        minTX -= 1; minTY -= 1;
        maxTX += 1; maxTY += 1;

        for (int tx = minTX; tx <= maxTX; tx++) {
            for (int ty = minTY; ty <= maxTY; ty++) {
                if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
                if (count >= MAX_UNITS + MAX_BUILDINGS + 128) break;
                u8 ttype = terrain.tileAt(tx, ty);
                if (ttype != TERRAIN_FOREST && ttype != TERRAIN_GOLD && ttype != TERRAIN_STONE &&
                    ttype != TERRAIN_BERRIES) continue;

                // Fog check — don't show resources in unexplored tiles
                if (!fogMap.isExplored(0, tx, ty)) continue;

                int isoX, isoY;
                tileToIso(tx, ty, isoX, isoY);
                // Generous bounds: an oak is 67px tall and 48 wide
                int sx = isoX - gs.camX;
                int sy = isoY - gs.camY;
                if (sx < -64 || sx >= SCREEN_W + 32 || sy < -16 || sy >= SCREEN_H + 72) continue;

                // Sort by bottom of tile (tile center bottom in iso)
                renderList[count].sortY = isoY + ISO_TILE_H;
                renderList[count].kind = 2;
                renderList[count].idx = 0;
                renderList[count].tx = tx;
                renderList[count].ty = ty;
                count++;
            }
        }
    }

    // Add visible buildings
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        if (!b.alive) continue;

        int pw = bldg_geom(b).w;
        int ph = bldg_geom(b).h;
        int tileW = BLDG_STATS[b.type].tileW;
        int tileH = BLDG_STATS[b.type].tileH;
        int bIsoX, bIsoY;
        worldToIso(b.x, b.y, bIsoX, bIsoY);
        int sx, sy;
        bldg_sprite_pos(b, gs, sx, sy);
        if (sx < -pw || sx >= SCREEN_W || sy < -ph || sy >= SCREEN_H) continue;

        // Fog check
        if (b.owner != 0) {
            int tx = b.x / TILE_PX;
            int ty = b.y / TILE_PX;
            if (!fogMap.isExplored(0, tx, ty)) continue;
        }

        // Sort by isoY of bottom-right corner (higher isoY = closer to camera)
        int bCornerIsoX, bCornerIsoY;
        worldToIso(b.x + tileW * TILE_PX, b.y + tileH * TILE_PX, bCornerIsoX, bCornerIsoY);
        renderList[count].sortY = bCornerIsoY;
        // The TC only stands on the back half of its footprint (the front is
        // open ground under its shadow), so units beside the front edges must
        // draw over it: sort it by the footprint centre instead.
        if (b.type == BLDG_TOWN_CENTER)
            renderList[count].sortY = (bIsoY + bCornerIsoY) / 2;
        renderList[count].kind = 0;
        renderList[count].idx = i;
        renderList[count].tx = 0;
        renderList[count].ty = 0;
        count++;
    }

    // Add visible units
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        if (!u.alive || u.state == USTATE_GARRISONED) continue;

        int gx, gy;
        unit_ground(u, gs, gx, gy);
        if (!unit_on_screen(gx, gy)) continue;

        // Fog check
        if (u.owner != 0) {
            int tx = (u.x + TILE_PX/2) / TILE_PX;
            int ty = (u.y + TILE_PX/2) / TILE_PX;
            if (!fogMap.isVisible(0, tx, ty)) continue;
        }

        // Sort by isoY of feet position (higher isoY = closer to camera)
        int feetIsoX, feetIsoY;
        worldToIso(u.x + TILE_PX/2, u.y + TILE_PX, feetIsoX, feetIsoY);
        renderList[count].sortY = feetIsoY;
        renderList[count].kind = 1;
        renderList[count].idx = i;
        renderList[count].tx = 0;
        renderList[count].ty = 0;
        count++;
    }

    // Sort by Y (back-to-front)
    sort_render_list(renderList, count);

    // --- Pre-render pass: draw selection indicators UNDER sprites ---
    // Selection circles for units
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!gs.unitSelected[i]) continue;
        Unit& u = units[i];
        if (!u.alive || u.state == USTATE_GARRISONED) continue;
        int uIsoX, uIsoY;
        worldToIso(u.x, u.y, uIsoX, uIsoY);
        int cx = uIsoX - gs.camX + ISO_TILE_W / 2;
        int cy = uIsoY - gs.camY + ISO_TILE_H / 2 + 1;
        if (cx >= -12 && cx < SCREEN_W + 12 && cy >= -6 && cy < SCREEN_H + 6) {
            draw_ellipse_buf(buf, cx, cy, 9, 4, PAL_WHITE);
        }
    }
    // Selection diamond for buildings
    if (gs.selectedBldg >= 0 && gs.selectedBldg < MAX_BUILDINGS && buildings[gs.selectedBldg].alive) {
        Building& b = buildings[gs.selectedBldg];
        int tileW = BLDG_STATS[b.type].tileW;
        int tileH = BLDG_STATS[b.type].tileH;
        int bIsoX, bIsoY;
        worldToIso(b.x, b.y, bIsoX, bIsoY);
        int bScreenX = bIsoX - gs.camX;
        int bScreenY = bIsoY - gs.camY;
        draw_selection_diamond(buf, bScreenX, bScreenY, tileW, tileH);
    }

    // Render in sorted order
    for (int r = 0; r < count; r++) {
        switch (renderList[r].kind) {
        case 0: render_building_sw(buf, gs, renderList[r].idx); break;
        case 1: render_unit_sw(buf, gs, renderList[r].idx); break;
        case 2: render_resource_sw(buf, gs, renderList[r].tx, renderList[r].ty, terrain); break;
        }
    }

    // --- Overlay pass: HP bars drawn on top of everything ---
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        if (!u.alive || u.state == USTATE_GARRISONED) continue;

        int gx, gy;
        unit_ground(u, gs, gx, gy);
        if (!unit_on_screen(gx, gy)) continue;

        if (u.owner != 0) {
            int tx = (u.x + TILE_PX/2) / TILE_PX;
            int ty = (u.y + TILE_PX/2) / TILE_PX;
            if (!fogMap.isVisible(0, tx, ty)) continue;
        }

        // Just above the head: the standing sheet's anchor row is its height
        draw_hp_bar(buf, gx, gy - unitStandSheet[u.type].g->ay - 4, 12,
                    u.hp, playerUnitStats[u.owner][u.type].hp, gs.unitSelected[i]);
    }

    // Building HP bars
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        if (!b.alive) continue;

        int tileW = BLDG_STATS[b.type].tileW;
        int tileH = BLDG_STATS[b.type].tileH;
        int bIsoX, bIsoY;
        worldToIso(b.x, b.y, bIsoX, bIsoY);
        int bScreenX = bIsoX - gs.camX;

        int pw = bldg_geom(b).w;
        int ph = bldg_geom(b).h;
        int bsx, bsy;
        bldg_sprite_pos(b, gs, bsx, bsy);
        if (bsx < -pw || bsx >= SCREEN_W || bsy < -ph || bsy >= SCREEN_H) continue;

        int hpCx = bScreenX + ISO_TILE_W / 2 + (tileW - tileH) * (ISO_TILE_W / 4);
        int hpW = (tileW + tileH) * 4 + 8;
        draw_hp_bar(buf, hpCx, bsy - 3, hpW, b.hp, BLDG_STATS[b.type].hp);
    }
}


// ---------------------------------------------------------------------------
// Build menu bar (drawn into bitmap buffer, uses palette indices)
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
// Train menu bar — the units a selected building trains
// ---------------------------------------------------------------------------
void render_train_menu(u8* vram, const GameState& gs) {
    if (gs.buildMenuOpen) return; // build menu takes priority
    if (gs.selectedBldg < 0) return;
    if (gs.selectionCount > 0) return; // units selected, not building
    const Building& b = buildings[gs.selectedBldg];
    if (!b.alive || b.owner != 0 || !building_is_complete(gs.selectedBldg)) return;

    // Every unit this building trains, in type order (input.cpp uses the same
    // order); ones that need a later age are shown locked
    int slot = 0;
    for (int ut = 0; ut < UNIT_TYPE_COUNT && slot < MENU_BAR_SLOTS; ut++) {
        if (UNIT_STATS[ut].bldgReq != b.type || UNIT_STATS[ut].trainTime == 0) continue;
        if (slot == 0) fill_menu_bar(vram);
        MenuItemState state =
            (gs.players[0].age < UNIT_STATS[ut].ageReq) ? MENU_ITEM_LOCKED :
            !game_can_afford(gs, 0, UNIT_STATS[ut].cost) ? MENU_ITEM_TOO_DEAR : MENU_ITEM_OK;
        draw_menu_icon(vram, slot, unitIcon[ut], state, ut == gs.trainUnitType);
        slot++;
    }
}

// ---------------------------------------------------------------------------
// Building placement preview — draws diamond-shaped tile highlights
// under the current touch position when in placement mode
// ---------------------------------------------------------------------------
void render_placement_preview(u8* buf, const GameState& gs, const TerrainMap& terrain) {
    if (gs.inputMode != 1) return;

    // Use current touch position if touching, otherwise last touch position
    int screenX = gs.touchActive ? gs.dragEndX : gs.dragStartX;
    int screenY = gs.touchActive ? gs.dragEndY : gs.dragStartY;

    // Convert screen coords to tile coords
    int baseTX, baseTY;
    screenToTile(screenX, screenY, gs.camX, gs.camY, baseTX, baseTY);

    const BuildingStats& st = BLDG_STATS[gs.placeBldgType];

    // Draw diamond highlight for each tile in the building footprint
    for (int dy = 0; dy < st.tileH; dy++) {
        for (int dx = 0; dx < st.tileW; dx++) {
            int tx = baseTX + dx;
            int ty = baseTY + dy;

            // Determine if this tile is valid for building
            bool valid = (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES &&
                          terrain.canBuild(tx, ty) && building_at_tile(tx, ty) < 0);

            u8 color = valid ? PAL_GREEN : PAL_RED;

            // Convert tile to iso screen position
            int isoX, isoY;
            tileToIso(tx, ty, isoX, isoY);
            int dstX = isoX - gs.camX;
            int dstY = isoY - gs.camY;

            // Draw diamond outline using the mask tables
            for (int py = 0; py < ISO_TILE_H; py++) {
                int sy = dstY + py;
                if (sy < 0 || sy >= SCREEN_H) continue;

                int xs = ISO_DIAMOND_XSTART[py];
                int xe = ISO_DIAMOND_XEND[py];

                // Draw only the outline (left edge, right edge, top/bottom row)
                for (int px = xs; px < xe; px++) {
                    if (px == xs || px == xe - 1 || py == 0 || py == ISO_TILE_H - 1) {
                        int sx = dstX + px;
                        if (sx >= 0 && sx < SCREEN_W) {
                            buf[sy * 256 + sx] = color;
                        }
                    }
                }
            }
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
