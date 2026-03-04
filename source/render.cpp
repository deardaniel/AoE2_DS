#include "render.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "iso.h"
#include "fog.h"
#include "tech.h"
#include "font.h"
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

// Unit sprite sheets (160x160 = 5 cols x 5 rows of 32x32 cells for walk/fight,
//                     160x96  = 5 cols x 3 rows for stand)
extern const u8 spr_villager_bin[];
extern const u8 spr_villager_walk_bin[];
extern const u8 spr_villager_attack_bin[];
extern const u8 spr_villager_f_bin[];
extern const u8 spr_villager_carry_bin[];
extern const u8 spr_lumberjack_bin[];
extern const u8 spr_lumberjack_walk_bin[];
extern const u8 spr_lumberjack_chop_bin[];
extern const u8 spr_lumberjack_carry_bin[];
extern const u8 spr_miner_bin[];
extern const u8 spr_miner_walk_bin[];
extern const u8 spr_miner_carry_bin[];
extern const u8 spr_builder_bin[];
extern const u8 spr_builder_walk_bin[];
extern const u8 spr_farmer_bin[];
extern const u8 spr_farmer_walk_bin[];
extern const u8 spr_farmer_carry_bin[];
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
extern const u8 spr_scout_die_bin[];
extern const u8 spr_villager_die_bin[];

// Building sprites (32x32 or 16x16, single frame)
extern const u8 spr_town_center_bin[];
extern const u8 spr_house_bin[];
extern const u8 spr_barracks_bin[];
extern const u8 spr_archery_range_bin[];
extern const u8 spr_stable_bin[];
extern const u8 spr_mining_camp_bin[];
extern const u8 spr_lumber_camp_bin[];

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

// Build menu layout (shared with input.cpp)
enum { BUILD_MENU_Y = 176, BUILD_MENU_H = 16, BUILD_MENU_ITEM_W = 32 };

// OAM slot management
static const int MAX_OAM_UNITS = 50;
static const int MAX_OAM_BLDGS = 30;
static const int OAM_UNIT_START = 0;
static const int OAM_BLDG_START = MAX_OAM_UNITS;
static const int OAM_UI_START = 80;

// Sprite sheet dimensions
static const int ANIM_SHEET_W = 320;  // 10 columns of 32px (walk/fight sheets)
static const int ANIM_SHEET_COLS = 10;
static const int STAND_SHEET_W = 160; // 5 columns of 32px (standing sheets)
static const int STAND_SHEET_COLS = 5;
static const int SHEET_ROWS = 5;      // max 5 rows
static const int CELL_W = 32;
static const int CELL_H = 32;

// ---------------------------------------------------------------------------
// Sprite sheet table — maps unit type + state to sheet data pointer
// ---------------------------------------------------------------------------

// Stand sheets indexed by UnitTypeId
static const u8* unitStandSheet[UNIT_TYPE_COUNT];
// Walk sheets (NULL if no walk sheet — fallback to stand)
static const u8* unitWalkSheet[UNIT_TYPE_COUNT];
// Attack/fight sheets (NULL if none — fallback to stand)
static const u8* unitFightSheet[UNIT_TYPE_COUNT];
// Death sheets (NULL if none — fallback to gray tint)
static const u8* unitDeathSheet[UNIT_TYPE_COUNT];

// Villager role-specific sprite sheets
// Indexed by VillagerRole: stand, walk, work, carry
static const u8* villagerRoleStand[VROLE_COUNT];
static const u8* villagerRoleWalk[VROLE_COUNT];
static const u8* villagerRoleWork[VROLE_COUNT];  // work animation (chop/mine/build/farm)
static const u8* villagerRoleCarry[VROLE_COUNT]; // carry-walk animation (returning)

// Building sheets indexed by BuildingTypeId
static const u8* buildingSheet[BLDG_TYPE_COUNT];
// Building sprite sizes (side length in pixels)
static int buildingSprW[BLDG_TYPE_COUNT];
static int buildingSprH[BLDG_TYPE_COUNT];

// ---------------------------------------------------------------------------
// Direction to sprite frame mapping
// AoE2 SLP sprites have 5 direction groups: S(0), SW(1), W(2), NW(3), N(4)
// East-facing directions are horizontal mirrors of their west-facing counterparts.
//
// Our 8 directions map to these SLP directions:
//   DIR_N(0)  -> SLP N  (4)
//   DIR_NE(1) -> SLP NW (3) + hFlip  (NE = mirror of NW)
//   DIR_E(2)  -> SLP W  (2) + hFlip  (E = mirror of W)
//   DIR_SE(3) -> SLP SW (1) + hFlip  (SE = mirror of SW)
//   DIR_S(4)  -> SLP S  (0)
//   DIR_SW(5) -> SLP SW (1)
//   DIR_W(6)  -> SLP W  (2)
//   DIR_NW(7) -> SLP NW (3)
// ---------------------------------------------------------------------------
static const int DIR_TO_FRAME[DIR_COUNT] = { 4, 3, 2, 1, 0, 1, 2, 3 };
static const bool DIR_HFLIP[DIR_COUNT] = { false, true, true, true, false, false, false, false };

// For walking/attack sheets (5 dirs x 10 anim frames = 50 frames):
// SLP Dir 0 (S): frames 0-9
// SLP Dir 1 (SW): frames 10-19
// SLP Dir 2 (W): frames 20-29
// SLP Dir 3 (NW): frames 30-39
// SLP Dir 4 (N): frames 40-49
static const int DIR_TO_ANIM_BASE[DIR_COUNT] = { 40, 30, 20, 10, 0, 10, 20, 30 };

// ---------------------------------------------------------------------------
// Convert linear pixel buffer to NDS 8x8 tile layout (256-color, 1D mapping)
// ---------------------------------------------------------------------------
static void linear_to_tiled(const u8* src, u8* dst, int w, int h) {
    int tilesX = w / 8;
    int tilesY = h / 8;
    int dstIdx = 0;
    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    int srcX = tx * 8 + px;
                    int srcY = ty * 8 + py;
                    dst[dstIdx++] = src[srcY * w + srcX];
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Convert linear pixel buffer to NDS 8x8 tile layout with horizontal flip
// ---------------------------------------------------------------------------
static void linear_to_tiled_hflip(const u8* src, u8* dst, int w, int h) {
    int tilesX = w / 8;
    int tilesY = h / 8;
    int dstIdx = 0;
    for (int ty = 0; ty < tilesY; ty++) {
        for (int tx = 0; tx < tilesX; tx++) {
            for (int py = 0; py < 8; py++) {
                for (int px = 0; px < 8; px++) {
                    int srcX = (w - 1) - (tx * 8 + px);
                    int srcY = ty * 8 + py;
                    dst[dstIdx++] = src[srcY * w + srcX];
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Extract a single 32x32 frame from a sprite sheet of given width/cols
// ---------------------------------------------------------------------------
static void extract_frame(const u8* sheet, int frameIdx, u8* dst32x32,
                          int sheetCols, int sheetW) {
    int col = frameIdx % sheetCols;
    int row = frameIdx / sheetCols;
    int srcX = col * CELL_W;
    int srcY = row * CELL_H;

    for (int y = 0; y < CELL_H; y++) {
        memcpy(&dst32x32[y * CELL_W],
               &sheet[(srcY + y) * sheetW + srcX],
               CELL_W);
    }
}

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
static const u8* get_unit_sheet(const Unit& u) {
    // Death animation (all unit types)
    if (u.state == USTATE_DEAD) {
        if (u.type == UNIT_VILLAGER) {
            return spr_villager_die_bin;
        }
        if (unitDeathSheet[u.type]) return unitDeathSheet[u.type];
        return unitStandSheet[u.type]; // fallback
    }

    // Villager role-specific sheets
    if (u.type == UNIT_VILLAGER) {
        int role = u.role;
        if (role >= VROLE_COUNT) role = VROLE_BASE;
        switch (u.state) {
        case USTATE_MOVING:
            // Show carry sprite when walking with resources (returning to drop-off)
            if (u.carryAmount > 0 && villagerRoleCarry[role])
                return villagerRoleCarry[role];
            if (villagerRoleWalk[role]) return villagerRoleWalk[role];
            break;
        case USTATE_RETURNING:
            if (villagerRoleCarry[role]) return villagerRoleCarry[role];
            if (villagerRoleWalk[role]) return villagerRoleWalk[role];
            break;
        case USTATE_GATHERING:
        case USTATE_BUILDING:
            if (villagerRoleWork[role]) return villagerRoleWork[role];
            if (villagerRoleWalk[role]) return villagerRoleWalk[role];
            break;
        case USTATE_ATTACKING:
            if (unitFightSheet[UNIT_VILLAGER]) return unitFightSheet[UNIT_VILLAGER];
            break;
        default:
            break;
        }
        return villagerRoleStand[role] ? villagerRoleStand[role] : unitStandSheet[UNIT_VILLAGER];
    }

    // Non-villager units
    switch (u.state) {
    case USTATE_MOVING:
    case USTATE_RETURNING:
    case USTATE_SCOUTING:
        if (unitWalkSheet[u.type]) return unitWalkSheet[u.type];
        break;
    case USTATE_ATTACKING:
    case USTATE_GATHERING:
        if (unitFightSheet[u.type]) return unitFightSheet[u.type];
        break;
    default:
        break;
    }
    return unitStandSheet[u.type];
}

// ---------------------------------------------------------------------------
// Check if a sheet is an animated sheet (walk/fight/work) vs stand
// ---------------------------------------------------------------------------
static bool is_animated_sheet(const u8* sheet, const Unit& u) {
    if (u.type == UNIT_VILLAGER) {
        int role = u.role;
        if (role >= VROLE_COUNT) role = VROLE_BASE;
        return sheet != villagerRoleStand[role] && sheet != unitStandSheet[u.type];
    }
    return sheet != unitStandSheet[u.type];
}

// Check if sheet is a carry sheet (5 frames per dir instead of 10)
static bool is_carry_sheet(const u8* sheet, const Unit& u) {
    if (u.type != UNIT_VILLAGER) return false;
    int role = u.role;
    if (role >= VROLE_COUNT) role = VROLE_BASE;
    return sheet == villagerRoleCarry[role] && villagerRoleCarry[role] != NULL;
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
    unitStandSheet[UNIT_VILLAGER]  = spr_villager_bin;
    unitStandSheet[UNIT_MILITIA]   = spr_militia_bin;
    unitStandSheet[UNIT_ARCHER]    = spr_archer_bin;
    unitStandSheet[UNIT_KNIGHT]    = spr_knight_bin;
    unitStandSheet[UNIT_SPEARMAN]  = spr_spearman_bin;
    unitStandSheet[UNIT_SCOUT]     = spr_scout_bin;

    unitWalkSheet[UNIT_VILLAGER]   = spr_villager_walk_bin;
    unitWalkSheet[UNIT_MILITIA]    = spr_militia_walk_bin;
    unitWalkSheet[UNIT_ARCHER]     = spr_archer_walk_bin;
    unitWalkSheet[UNIT_KNIGHT]     = spr_knight_walk_bin;
    unitWalkSheet[UNIT_SPEARMAN]   = spr_spearman_walk_bin;
    unitWalkSheet[UNIT_SCOUT]      = spr_scout_walk_bin;

    unitFightSheet[UNIT_VILLAGER]  = spr_villager_attack_bin;
    unitFightSheet[UNIT_MILITIA]   = spr_militia_fight_bin;
    unitFightSheet[UNIT_ARCHER]    = spr_archer_fire_bin;
    unitFightSheet[UNIT_KNIGHT]    = spr_knight_fight_bin;
    unitFightSheet[UNIT_SPEARMAN]  = spr_spearman_fight_bin;
    unitFightSheet[UNIT_SCOUT]     = spr_scout_bin;  // scout uses idle for "attack"

    unitDeathSheet[UNIT_VILLAGER]  = spr_villager_die_bin;
    unitDeathSheet[UNIT_MILITIA]   = spr_militia_die_bin;
    unitDeathSheet[UNIT_ARCHER]    = spr_archer_die_bin;
    unitDeathSheet[UNIT_KNIGHT]    = spr_knight_die_bin;
    unitDeathSheet[UNIT_SPEARMAN]  = spr_spearman_die_bin;
    unitDeathSheet[UNIT_SCOUT]     = spr_scout_die_bin;

    // Villager role-specific sheets
    villagerRoleStand[VROLE_BASE]       = spr_villager_bin;
    villagerRoleWalk[VROLE_BASE]        = spr_villager_walk_bin;
    villagerRoleWork[VROLE_BASE]        = spr_villager_attack_bin;
    villagerRoleCarry[VROLE_BASE]       = spr_villager_carry_bin;

    villagerRoleStand[VROLE_LUMBERJACK] = spr_lumberjack_bin;
    villagerRoleWalk[VROLE_LUMBERJACK]  = spr_lumberjack_walk_bin;
    villagerRoleWork[VROLE_LUMBERJACK]  = spr_lumberjack_chop_bin;
    villagerRoleCarry[VROLE_LUMBERJACK] = spr_lumberjack_carry_bin;

    villagerRoleStand[VROLE_MINER]      = spr_miner_bin;
    villagerRoleWalk[VROLE_MINER]       = spr_miner_walk_bin;
    villagerRoleWork[VROLE_MINER]       = spr_villager_attack_bin; // no mining SLP in AoE2 HD
    villagerRoleCarry[VROLE_MINER]      = spr_miner_carry_bin;

    villagerRoleStand[VROLE_BUILDER]    = spr_builder_bin;
    villagerRoleWalk[VROLE_BUILDER]     = spr_builder_walk_bin;
    villagerRoleWork[VROLE_BUILDER]     = spr_villager_attack_bin; // no builder SLP in AoE2 HD
    villagerRoleCarry[VROLE_BUILDER]    = spr_villager_carry_bin;  // builders use base carry

    villagerRoleStand[VROLE_FARMER]     = spr_farmer_bin;
    villagerRoleWalk[VROLE_FARMER]      = spr_farmer_walk_bin;
    villagerRoleWork[VROLE_FARMER]      = spr_villager_attack_bin; // no farmer SLP in AoE2 HD
    villagerRoleCarry[VROLE_FARMER]     = spr_farmer_carry_bin;

    // Set up building sheet lookup tables
    buildingSheet[BLDG_TOWN_CENTER]   = spr_town_center_bin;
    buildingSheet[BLDG_HOUSE]         = spr_house_bin;
    buildingSheet[BLDG_BARRACKS]      = spr_barracks_bin;
    buildingSheet[BLDG_ARCHERY_RANGE] = spr_archery_range_bin;
    buildingSheet[BLDG_STABLE]        = spr_stable_bin;
    buildingSheet[BLDG_FARM]          = NULL;  // farm rendered as terrain
    buildingSheet[BLDG_MINING_CAMP]   = spr_mining_camp_bin;
    buildingSheet[BLDG_LUMBER_CAMP]   = spr_lumber_camp_bin;

    // Building sprite pixel sizes (must match preprocessing target sizes)
    // Isometric: footW = (tileW+tileH)*16, sprH = footH + 16 above-ground
    buildingSprW[BLDG_TOWN_CENTER]   = 128; buildingSprH[BLDG_TOWN_CENTER]   = 96;
    buildingSprW[BLDG_HOUSE]         = 32;  buildingSprH[BLDG_HOUSE]         = 32;
    buildingSprW[BLDG_BARRACKS]      = 64;  buildingSprH[BLDG_BARRACKS]      = 48;
    buildingSprW[BLDG_ARCHERY_RANGE] = 64;  buildingSprH[BLDG_ARCHERY_RANGE] = 48;
    buildingSprW[BLDG_STABLE]        = 64;  buildingSprH[BLDG_STABLE]        = 48;
    buildingSprW[BLDG_FARM]          = 32;  buildingSprH[BLDG_FARM]          = 32;
    buildingSprW[BLDG_MINING_CAMP]   = 32;  buildingSprH[BLDG_MINING_CAMP]   = 32;
    buildingSprW[BLDG_LUMBER_CAMP]   = 32;  buildingSprH[BLDG_LUMBER_CAMP]   = 32;
}

// ---------------------------------------------------------------------------
// Compute building sprite screen offset from worldToIso origin
// ---------------------------------------------------------------------------
static void bldg_sprite_offset(int tileW, int tileH, int ph, int& offX, int& offY) {
    offX = -tileH * (ISO_TILE_W / 2);
    int footH = (tileW + tileH) * (ISO_TILE_H / 2);
    offY = -(ph - footH);
}

// ---------------------------------------------------------------------------
// Render all sprites
// ---------------------------------------------------------------------------
void render_sprites(const GameState& gs, const TerrainMap& terrain) {
    // First, hide all OAM entries
    for (int i = 0; i < 128; i++) {
        oamSet(&oamSub, i, 0, 192, 0, 0, SpriteSize_16x16, SpriteColorFormat_256Color,
               NULL, -1, false, true, false, false, false);
    }

    int oamIdx = OAM_UNIT_START;

    // --- Render units (32x32 OAM sprites from HD sprite sheets) ---
    for (int i = 0; i < MAX_UNITS && oamIdx < OAM_BLDG_START; i++) {
        Unit& u = units[i];
        if (!u.alive || u.state == USTATE_GARRISONED) continue;

        // Check if on screen (32x32 sprite, positioned at iso coords)
        int uIsoX, uIsoY;
        worldToIso(u.x, u.y, uIsoX, uIsoY);
        int sx = uIsoX - gs.camX;
        int sy = uIsoY - gs.camY - (CELL_H - ISO_TILE_H);
        if (sx < -CELL_W || sx >= SCREEN_W || sy < -CELL_H || sy >= SCREEN_H) continue;

        // Fog check — only show enemy units in visible tiles
        if (u.owner != 0) {
            int tx = (u.x + TILE_PX / 2) / TILE_PX;
            int ty = (u.y + TILE_PX / 2) / TILE_PX;
            if (!fogMap.isVisible(0, tx, ty)) continue;
        }

        // Allocate OAM gfx if needed (32x32 = 1024 bytes)
        if (!u.spriteGfx) {
            u.spriteGfx = oamAllocateGfx(&oamSub, SpriteSize_32x32, SpriteColorFormat_256Color);
        }
        if (!u.spriteGfx) continue;

        // Select sprite sheet based on unit state and role
        const u8* sheet = get_unit_sheet(u);

        // Select frame based on direction and animation
        int frameIdx;
        bool hflip;
        bool animated = is_animated_sheet(sheet, u);
        if (animated) {
            bool carry = is_carry_sheet(sheet, u);
            int base = carry ? DIR_TO_ANIM_BASE[u.direction] / 2 : DIR_TO_ANIM_BASE[u.direction];
            int fpd = carry ? 5 : 10;
            int anim = u.animFrame % fpd;
            frameIdx = base + anim;
            hflip = DIR_HFLIP[u.direction];
        } else {
            // Stand sheet: 5 frames, one per direction
            frameIdx = DIR_TO_FRAME[u.direction];
            hflip = DIR_HFLIP[u.direction];
        }

        int sheetCols = animated ? ANIM_SHEET_COLS : STAND_SHEET_COLS;
        int sheetW = animated ? ANIM_SHEET_W : STAND_SHEET_W;

        // Clamp frame index to valid range
        if (frameIdx >= sheetCols * SHEET_ROWS) frameIdx = 0;

        // Extract 32x32 frame from sheet
        u8 frame[CELL_W * CELL_H];
        extract_frame(sheet, frameIdx, frame, sheetCols, sheetW);

        // Apply player 2 color remap
        if (u.owner == 1) {
            apply_color_remap(frame, CELL_W * CELL_H);
        }

        // Convert to NDS tiled format and copy to OAM VRAM
        u8 tiled[CELL_W * CELL_H];
        if (hflip) {
            linear_to_tiled_hflip(frame, tiled, CELL_W, CELL_H);
        } else {
            linear_to_tiled(frame, tiled, CELL_W, CELL_H);
        }
        dmaCopy(tiled, u.spriteGfx, CELL_W * CELL_H);

        bool selected = gs.unitSelected[i];
        oamSet(&oamSub, oamIdx, sx, sy, 0, 0, SpriteSize_32x32, SpriteColorFormat_256Color,
               u.spriteGfx, -1, false, false, selected, false, false);
        u.oamSlot = oamIdx;
        oamIdx++;
    }

    // --- Render buildings ---
    for (int i = 0; i < MAX_BUILDINGS && oamIdx < OAM_UI_START; i++) {
        Building& b = buildings[i];
        if (!b.alive) continue;

        const BuildingStats& st = BLDG_STATS[b.type];
        int pw = buildingSprW[b.type];
        int ph = buildingSprH[b.type];
        int tileW = st.tileW;
        int tileH = st.tileH;

        int bIsoX, bIsoY;
        worldToIso(b.x, b.y, bIsoX, bIsoY);
        int offX, offY;
        bldg_sprite_offset(tileW, tileH, ph, offX, offY);
        int sx = bIsoX - gs.camX + offX;
        int sy = bIsoY - gs.camY + offY;
        if (sx < -pw || sx >= SCREEN_W || sy < -ph || sy >= SCREEN_H) continue;

        // Fog check
        if (b.owner != 0) {
            int tx = b.x / TILE_PX;
            int ty = b.y / TILE_PX;
            if (!fogMap.isExplored(0, tx, ty)) continue;
        }

        bool complete = (b.buildProgress >= st.buildTime);

        // OAM can only handle power-of-2 sizes up to 64x64; skip oversized buildings
        if (pw > 64 || ph > 64) continue;

        // Determine OAM sprite size
        SpriteSize sprSize;
        if (pw > 32 || ph > 32) {
            sprSize = SpriteSize_64x64;
        } else if (pw > 16 || ph > 16) {
            sprSize = SpriteSize_32x32;
        } else {
            sprSize = SpriteSize_16x16;
        }

        if (!b.spriteGfx) {
            b.spriteGfx = oamAllocateGfx(&oamSub, sprSize, SpriteColorFormat_256Color);
        }
        if (!b.spriteGfx) continue;

        u8 oamBuf[64 * 64];
        memset(oamBuf, 0, sizeof(oamBuf));

        if (!complete || buildingSheet[b.type] == NULL) {
            for (int y = 0; y < ph; y++) {
                for (int x = 0; x < pw; x++) {
                    bool border = (x == 0 || y == 0 || x == pw - 1 || y == ph - 1);
                    if (border || ((x + y) % 6 == 0)) {
                        oamBuf[y * pw + x] = 1;
                    }
                }
            }
        } else {
            memcpy(oamBuf, buildingSheet[b.type], pw * ph);
            if (b.owner == 1) {
                apply_color_remap(oamBuf, pw * ph);
            }
        }

        u8 tiled[64 * 64];
        linear_to_tiled(oamBuf, tiled, pw, ph);
        dmaCopy(tiled, b.spriteGfx, pw * ph);

        bool selected = (gs.selectedBldg == i);
        oamSet(&oamSub, oamIdx, sx, sy, 0, 0, sprSize, SpriteColorFormat_256Color,
               b.spriteGfx, -1, false, false, selected, false, false);
        b.oamSlot = oamIdx;
        oamIdx++;
    }

    oamUpdate(&oamSub);
}

// ---------------------------------------------------------------------------
// Draw a horizontal HP bar into the bitmap buffer
// ---------------------------------------------------------------------------
static void draw_hp_bar(u8* buf, int cx, int sy, int barW, int hp, int maxHp) {
    if (maxHp <= 0) return;
    int filledW = (hp * barW) / maxHp;
    if (filledW < 0) filledW = 0;
    if (filledW > barW) filledW = barW;
    int x0 = cx - barW / 2;

    // Only draw if damaged
    if (hp >= maxHp) return;

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
    int pw = buildingSprW[b.type];
    int ph = buildingSprH[b.type];
    int tileW = BLDG_STATS[b.type].tileW;
    int tileH = BLDG_STATS[b.type].tileH;

    int bIsoX, bIsoY;
    worldToIso(b.x, b.y, bIsoX, bIsoY);
    int bScreenX = bIsoX - gs.camX;
    int bScreenY = bIsoY - gs.camY;

    // Offset for diamond footprint: left extension + above-ground height
    int offX, offY;
    bldg_sprite_offset(tileW, tileH, ph, offX, offY);
    int sx = bScreenX + offX;
    int sy = bScreenY + offY;

    bool complete = (b.buildProgress >= BLDG_STATS[b.type].buildTime);
    u8 frame[128 * 96]; // max building sprite size (TC 4x4 = 128x96)
    memset(frame, 0, sizeof(frame));

    if (!complete || buildingSheet[b.type] == NULL) {
        for (int y = 0; y < ph; y++)
            for (int x = 0; x < pw; x++) {
                bool border = (x == 0 || y == 0 || x == pw-1 || y == ph-1);
                if (border || ((x + y) % 6 == 0))
                    frame[y * pw + x] = PAL_BROWN;
            }
    } else {
        memcpy(frame, buildingSheet[b.type], pw * ph);
        if (b.owner == 1) apply_color_remap(frame, pw * ph);
    }

    blit_frame(buf, frame, pw, ph, sx, sy, false);
}

// ---------------------------------------------------------------------------
// Draw a resource sprite (tree, gold mine, stone mine) into the bitmap buffer
// ---------------------------------------------------------------------------
static void render_resource_sw(u8* buf, const GameState& gs, int tx, int ty, const TerrainMap& terrain) {
    u8 ttype = terrain.tileAt(tx, ty);
    const u8* spr = terrain_get_resource_sprite(ttype);
    if (!spr) return;

    int isoX, isoY;
    tileToIso(tx, ty, isoX, isoY);
    int sx = isoX - gs.camX;
    int sy = isoY - gs.camY - (32 - ISO_TILE_H);  // offset up so sprite rises above diamond

    for (int py = 0; py < 32; py++) {
        int screenY = sy + py;
        if (screenY < 0 || screenY >= SCREEN_H) continue;
        for (int px = 0; px < 32; px++) {
            int screenX = sx + px;
            if (screenX < 0 || screenX >= SCREEN_W) continue;
            u8 val = spr[py * 32 + px];
            if (val != 0) buf[screenY * 256 + screenX] = val;
        }
    }

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

    int uIsoX, uIsoY;
    worldToIso(u.x, u.y, uIsoX, uIsoY);
    int sx = uIsoX - gs.camX;
    int sy = uIsoY - gs.camY - (CELL_H - ISO_TILE_H);

    const u8* sheet = get_unit_sheet(u);
    int frameIdx;
    bool hflip;
    bool animated = is_animated_sheet(sheet, u);
    if (animated) {
        bool carry = is_carry_sheet(sheet, u);
        int base = carry ? DIR_TO_ANIM_BASE[u.direction] / 2 : DIR_TO_ANIM_BASE[u.direction];
        int fpd = carry ? 5 : 10;
        int anim = u.animFrame % fpd;
        frameIdx = base + anim;
        hflip = DIR_HFLIP[u.direction];
    } else {
        frameIdx = DIR_TO_FRAME[u.direction];
        hflip = DIR_HFLIP[u.direction];
    }
    int sheetCols = animated ? ANIM_SHEET_COLS : STAND_SHEET_COLS;
    int sheetW = animated ? ANIM_SHEET_W : STAND_SHEET_W;
    if (frameIdx >= sheetCols * SHEET_ROWS) frameIdx = 0;

    u8 frame[CELL_W * CELL_H];
    extract_frame(sheet, frameIdx, frame, sheetCols, sheetW);
    if (u.owner == 1) apply_color_remap(frame, CELL_W * CELL_H);

    blit_frame(buf, frame, CELL_W, CELL_H, sx, sy, hflip);
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
                if (ttype != TERRAIN_FOREST && ttype != TERRAIN_GOLD && ttype != TERRAIN_STONE) continue;

                // Fog check — don't show resources in unexplored tiles
                if (!fogMap.isExplored(0, tx, ty)) continue;

                int isoX, isoY;
                tileToIso(tx, ty, isoX, isoY);
                int sx = isoX - gs.camX;
                int sy = isoY - gs.camY - (32 - ISO_TILE_H);
                if (sx + 32 <= 0 || sx >= SCREEN_W || sy + 32 <= 0 || sy >= SCREEN_H) continue;

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

        int pw = buildingSprW[b.type];
        int ph = buildingSprH[b.type];
        int tileW = BLDG_STATS[b.type].tileW;
        int tileH = BLDG_STATS[b.type].tileH;
        int bIsoX, bIsoY;
        worldToIso(b.x, b.y, bIsoX, bIsoY);
        int offX, offY;
        bldg_sprite_offset(tileW, tileH, ph, offX, offY);
        int sx = bIsoX - gs.camX + offX;
        int sy = bIsoY - gs.camY + offY;
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

        int uIsoX, uIsoY;
        worldToIso(u.x, u.y, uIsoX, uIsoY);
        int sx = uIsoX - gs.camX;
        int sy = uIsoY - gs.camY - (CELL_H - ISO_TILE_H);
        if (sx < -CELL_W || sx >= SCREEN_W || sy < -CELL_H || sy >= SCREEN_H) continue;

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
        int cy = uIsoY - gs.camY + ISO_TILE_H / 2 + 6;
        if (cx >= -12 && cx < SCREEN_W + 12 && cy >= -6 && cy < SCREEN_H + 6) {
            draw_ellipse_buf(buf, cx, cy, 12, 6, PAL_WHITE);
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

        int uIsoX, uIsoY;
        worldToIso(u.x, u.y, uIsoX, uIsoY);
        int sx = uIsoX - gs.camX;
        int sy = uIsoY - gs.camY - (CELL_H - ISO_TILE_H);

        if (sx < -CELL_W || sx >= SCREEN_W || sy < -CELL_H || sy >= SCREEN_H) continue;

        if (u.owner != 0) {
            int tx = (u.x + TILE_PX/2) / TILE_PX;
            int ty = (u.y + TILE_PX/2) / TILE_PX;
            if (!fogMap.isVisible(0, tx, ty)) continue;
        }

        draw_hp_bar(buf, sx + CELL_W / 2, sy - 3, 16, u.hp, playerUnitStats[u.owner][u.type].hp);
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
        int bScreenY = bIsoY - gs.camY;

        int pw = buildingSprW[b.type];
        int ph = buildingSprH[b.type];
        int offX, offY;
        bldg_sprite_offset(tileW, tileH, ph, offX, offY);
        int bsx = bScreenX + offX;
        int bsy = bScreenY + offY;
        if (bsx < -pw || bsx >= SCREEN_W || bsy < -ph || bsy >= SCREEN_H) continue;

        int hpCx = bScreenX + ISO_TILE_W / 2;
        int hpW = (tileW + tileH) * 8;
        draw_hp_bar(buf, hpCx, bScreenY - 3, hpW, b.hp, BLDG_STATS[b.type].hp);
    }
}

// Building abbreviations for the menu
static const char* BLDG_ABBREV[BLDG_TYPE_COUNT] = {
    "TC", "Hs", "Bk", "AR", "SB", "Fm", "Mc", "Lc"
};

// ---------------------------------------------------------------------------
// Build menu bar (drawn into bitmap buffer, uses palette indices)
// ---------------------------------------------------------------------------
void render_build_menu(u8* vram, const GameState& gs) {
    if (!gs.buildMenuOpen) return;

    // Draw a bar at bottom of screen
    for (int y = BUILD_MENU_Y; y < SCREEN_H; y++) {
        for (int x = 0; x < SCREEN_W; x++) {
            vram[y * 256 + x] = PAL_DARKBROWN;
        }
    }

    // Draw icons for each building type
    for (int i = 0; i < BLDG_TYPE_COUNT; i++) {
        int ix = i * BUILD_MENU_ITEM_W;
        if (ix + BUILD_MENU_ITEM_W > SCREEN_W) break;

        bool available = (gs.players[0].age >= BLDG_STATS[i].ageReq);
        bool affordable = game_can_afford(gs, 0, BLDG_STATS[i].cost);
        u8 textColor = (available && affordable) ? PAL_WHITE : PAL_GRAY;
        u8 bgColor = (available && affordable) ? PAL_BROWN : PAL_DARKGRAY;

        // Fill slot background
        for (int y = BUILD_MENU_Y + 1; y < SCREEN_H - 1; y++) {
            for (int x = ix + 1; x < ix + BUILD_MENU_ITEM_W - 1; x++) {
                if (x < SCREEN_W) vram[y * 256 + x] = bgColor;
            }
        }

        // Draw abbreviation text, centered in slot
        int textW = font_string_width(gameFont, BLDG_ABBREV[i]);
        int tx = ix + (BUILD_MENU_ITEM_W - textW) / 2;
        int ty = BUILD_MENU_Y + (BUILD_MENU_H - gameFont.height) / 2;
        font_draw_str_8(vram, 256, SCREEN_H, tx, ty, BLDG_ABBREV[i], textColor, gameFont);

        // Border between items
        for (int y = BUILD_MENU_Y; y < SCREEN_H; y++) {
            int bx = ix + BUILD_MENU_ITEM_W - 1;
            if (bx < SCREEN_W) vram[y * 256 + bx] = PAL_BLACK;
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
