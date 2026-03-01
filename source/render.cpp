#include "render.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "fog.h"
#include "tech.h"
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

// Unit sprite sheets (160x96 = 5 cols x 3 rows of 32x32 cells)
extern const u8 spr_villager_bin[];
extern const u8 spr_villager_walk_bin[];
extern const u8 spr_villager_f_bin[];
extern const u8 spr_militia_bin[];
extern const u8 spr_militia_fight_bin[];
extern const u8 spr_archer_bin[];
extern const u8 spr_archer_fire_bin[];
extern const u8 spr_knight_bin[];
extern const u8 spr_knight_fight_bin[];
extern const u8 spr_spearman_bin[];
extern const u8 spr_spearman_fight_bin[];

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
static const int SHEET_W = 160;   // 5 columns of 32px
static const int SHEET_H = 96;    // 3 rows of 32px
static const int CELL_W = 32;
static const int CELL_H = 32;
static const int SHEET_COLS = 5;
static const int SHEET_ROWS = 3;

// ---------------------------------------------------------------------------
// Sprite sheet table — maps unit type + state to sheet data pointer
// ---------------------------------------------------------------------------

// Stand sheets indexed by UnitTypeId
static const u8* unitStandSheet[UNIT_TYPE_COUNT];
// Walk sheets (NULL if no walk sheet — fallback to stand)
static const u8* unitWalkSheet[UNIT_TYPE_COUNT];
// Attack/fight sheets (NULL if none — fallback to stand)
static const u8* unitFightSheet[UNIT_TYPE_COUNT];

// Building sheets indexed by BuildingTypeId
static const u8* buildingSheet[BLDG_TYPE_COUNT];
// Building sprite sizes (side length in pixels)
static int buildingSprW[BLDG_TYPE_COUNT];
static int buildingSprH[BLDG_TYPE_COUNT];

// ---------------------------------------------------------------------------
// Direction to sprite frame mapping
// AoE2 SLP standing sprites have 5 frames: S, SW, W, NW, N
// Our 4-direction system maps as:
//   DIR_UP(0)    -> frame 4 (N)
//   DIR_RIGHT(1) -> frame 2 (W) + hFlip
//   DIR_DOWN(2)  -> frame 0 (S)
//   DIR_LEFT(3)  -> frame 2 (W)
// ---------------------------------------------------------------------------
static const int DIR_TO_FRAME[DIR_COUNT] = { 4, 2, 0, 2 };
static const bool DIR_HFLIP[DIR_COUNT] = { false, true, false, false };

// For walking/attack sheets (5 dirs x 3 anim frames = 15 frames):
// Dir 0 (S): frames 0,1,2
// Dir 1 (SW): frames 3,4,5 (not used directly)
// Dir 2 (W): frames 6,7,8
// Dir 3 (NW): frames 9,10,11 (not used directly)
// Dir 4 (N): frames 12,13,14
static const int DIR_TO_ANIM_BASE[DIR_COUNT] = { 12, 6, 0, 6 };

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
// Extract a single 32x32 frame from a 160x96 sprite sheet
// ---------------------------------------------------------------------------
static void extract_frame(const u8* sheet, int frameIdx, u8* dst32x32) {
    int col = frameIdx % SHEET_COLS;
    int row = frameIdx / SHEET_COLS;
    int srcX = col * CELL_W;
    int srcY = row * CELL_H;

    for (int y = 0; y < CELL_H; y++) {
        memcpy(&dst32x32[y * CELL_W],
               &sheet[(srcY + y) * SHEET_W + srcX],
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
// Get the appropriate sprite sheet for a unit based on its state
// ---------------------------------------------------------------------------
static const u8* get_unit_sheet(int unitType, int unitState) {
    switch (unitState) {
    case USTATE_MOVING:
    case USTATE_RETURNING:
        if (unitWalkSheet[unitType]) return unitWalkSheet[unitType];
        break;
    case USTATE_ATTACKING:
    case USTATE_GATHERING:
        if (unitFightSheet[unitType]) return unitFightSheet[unitType];
        break;
    default:
        break;
    }
    return unitStandSheet[unitType];
}

// ---------------------------------------------------------------------------
// Check if a sheet is an animated sheet (walk/fight) vs stand
// ---------------------------------------------------------------------------
static bool is_animated_sheet(const u8* sheet, int unitType) {
    return sheet != unitStandSheet[unitType];
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

    unitWalkSheet[UNIT_VILLAGER]   = spr_villager_walk_bin;
    unitWalkSheet[UNIT_MILITIA]    = NULL;  // no walk sheet, use stand
    unitWalkSheet[UNIT_ARCHER]     = NULL;
    unitWalkSheet[UNIT_KNIGHT]     = NULL;
    unitWalkSheet[UNIT_SPEARMAN]   = NULL;

    unitFightSheet[UNIT_VILLAGER]  = NULL;  // no fight sheet for villager
    unitFightSheet[UNIT_MILITIA]   = spr_militia_fight_bin;
    unitFightSheet[UNIT_ARCHER]    = spr_archer_fire_bin;
    unitFightSheet[UNIT_KNIGHT]    = spr_knight_fight_bin;
    unitFightSheet[UNIT_SPEARMAN]  = spr_spearman_fight_bin;

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
    buildingSprW[BLDG_TOWN_CENTER]   = 32;  buildingSprH[BLDG_TOWN_CENTER]   = 32;
    buildingSprW[BLDG_HOUSE]         = 16;  buildingSprH[BLDG_HOUSE]         = 16;
    buildingSprW[BLDG_BARRACKS]      = 32;  buildingSprH[BLDG_BARRACKS]      = 32;
    buildingSprW[BLDG_ARCHERY_RANGE] = 32;  buildingSprH[BLDG_ARCHERY_RANGE] = 32;
    buildingSprW[BLDG_STABLE]        = 32;  buildingSprH[BLDG_STABLE]        = 32;
    buildingSprW[BLDG_FARM]          = 16;  buildingSprH[BLDG_FARM]          = 16;
    buildingSprW[BLDG_MINING_CAMP]   = 16;  buildingSprH[BLDG_MINING_CAMP]   = 16;
    buildingSprW[BLDG_LUMBER_CAMP]   = 16;  buildingSprH[BLDG_LUMBER_CAMP]   = 16;
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
        if (!u.alive) continue;

        // Check if on screen (32x32 sprite, centered on unit tile pos)
        int sx = u.x - gs.camX - 8;  // offset to center 32px sprite on 16px tile
        int sy = u.y - gs.camY - 8;
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

        // Select sprite sheet based on unit state
        const u8* sheet = get_unit_sheet(u.type, u.state);

        // Select frame based on direction and animation
        int frameIdx;
        bool hflip;
        if (is_animated_sheet(sheet, u.type)) {
            // Animated sheet: 5 dirs x 3 anim frames
            int base = DIR_TO_ANIM_BASE[u.direction];
            int anim = u.animFrame % 3;
            frameIdx = base + anim;
            hflip = DIR_HFLIP[u.direction];
        } else {
            // Stand sheet: 5 frames, one per direction
            frameIdx = DIR_TO_FRAME[u.direction];
            hflip = DIR_HFLIP[u.direction];
        }

        // Clamp frame index to valid range
        if (frameIdx >= SHEET_COLS * SHEET_ROWS) frameIdx = 0;

        // Extract 32x32 frame from sheet
        u8 frame[CELL_W * CELL_H];
        extract_frame(sheet, frameIdx, frame);

        // Apply player 2 color remap
        if (u.owner == 1) {
            apply_color_remap(frame, CELL_W * CELL_H);
        }

        // Death tint: darken all non-transparent pixels to dark gray
        if (u.state == USTATE_DEAD) {
            for (int j = 0; j < CELL_W * CELL_H; j++) {
                if (frame[j] != 0) frame[j] = 255; // palette 255 = death tint
            }
        }

        // Convert to NDS tiled format and copy to OAM VRAM
        u8 tiled[CELL_W * CELL_H];
        if (hflip) {
            linear_to_tiled_hflip(frame, tiled, CELL_W, CELL_H);
        } else {
            linear_to_tiled(frame, tiled, CELL_W, CELL_H);
        }
        dmaCopy(tiled, u.spriteGfx, CELL_W * CELL_H);

        bool selected = (gs.selectedUnit == i);
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

        int sx = b.x - gs.camX;
        int sy = b.y - gs.camY;
        if (sx < -pw || sx >= SCREEN_W || sy < -ph || sy >= SCREEN_H) continue;

        // Fog check
        if (b.owner != 0) {
            int tx = b.x / TILE_PX;
            int ty = b.y / TILE_PX;
            if (!fogMap.isExplored(0, tx, ty)) continue;
        }

        bool complete = (b.buildProgress >= st.buildTime);

        // Determine OAM sprite size
        SpriteSize sprSize;
        if (pw >= 32) {
            sprSize = SpriteSize_32x32;
        } else {
            sprSize = SpriteSize_16x16;
        }

        if (!b.spriteGfx) {
            b.spriteGfx = oamAllocateGfx(&oamSub, sprSize, SpriteColorFormat_256Color);
        }
        if (!b.spriteGfx) continue;

        u8 buf[32 * 32]; // max building sprite size
        memset(buf, 0, sizeof(buf));

        if (!complete || buildingSheet[b.type] == NULL) {
            // Under construction or no sprite: show simple construction pattern
            for (int y = 0; y < ph; y++) {
                for (int x = 0; x < pw; x++) {
                    bool border = (x == 0 || y == 0 || x == pw - 1 || y == ph - 1);
                    if (border || ((x + y) % 6 == 0)) {
                        buf[y * pw + x] = 1; // use first palette entry (dark)
                    }
                }
            }
        } else {
            // Copy HD building sprite data
            memcpy(buf, buildingSheet[b.type], pw * ph);

            // Apply player 2 color remap
            if (b.owner == 1) {
                apply_color_remap(buf, pw * ph);
            }
        }

        // Convert to tiled format and copy to OAM VRAM
        u8 tiled[32 * 32];
        linear_to_tiled(buf, tiled, pw, ph);
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
// Software-render all visible units and buildings into bitmap buffer
// Uses sprite palette indices — caller must ensure sprite palette is also
// loaded into BG_PALETTE_SUB (shared palette) for correct colors.
// ---------------------------------------------------------------------------
void render_sprites_sw(u8* buf, const GameState& gs, const TerrainMap& terrain) {
    // --- Render buildings first (behind units) ---
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        Building& b = buildings[i];
        if (!b.alive) continue;

        int pw = buildingSprW[b.type];
        int ph = buildingSprH[b.type];

        int sx = b.x - gs.camX;
        int sy = b.y - gs.camY;
        if (sx < -pw || sx >= SCREEN_W || sy < -ph || sy >= SCREEN_H) continue;

        // Fog check
        if (b.owner != 0) {
            int tx = b.x / TILE_PX;
            int ty = b.y / TILE_PX;
            if (!fogMap.isExplored(0, tx, ty)) continue;
        }

        bool complete = (b.buildProgress >= BLDG_STATS[b.type].buildTime);
        u8 frame[32 * 32];
        memset(frame, 0, sizeof(frame));

        if (!complete || buildingSheet[b.type] == NULL) {
            // Under construction: simple pattern
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

        // Selection highlight: draw border
        if (gs.selectedBldg == i) {
            for (int x = 0; x < pw; x++) { frame[x] = PAL_WHITE; frame[(ph-1)*pw+x] = PAL_WHITE; }
            for (int y = 0; y < ph; y++) { frame[y*pw] = PAL_WHITE; frame[y*pw+pw-1] = PAL_WHITE; }
        }

        blit_frame(buf, frame, pw, ph, sx, sy, false);

        // HP bar above building
        draw_hp_bar(buf, sx + pw / 2, sy - 3, pw, b.hp, BLDG_STATS[b.type].hp);
    }

    // --- Render units (on top of buildings) ---
    for (int i = 0; i < MAX_UNITS; i++) {
        Unit& u = units[i];
        if (!u.alive) continue;

        int sx = u.x - gs.camX - 8;
        int sy = u.y - gs.camY - 8;
        if (sx < -CELL_W || sx >= SCREEN_W || sy < -CELL_H || sy >= SCREEN_H) continue;

        // Fog check — only show enemy units in visible tiles
        if (u.owner != 0) {
            int tx = (u.x + TILE_PX/2) / TILE_PX;
            int ty = (u.y + TILE_PX/2) / TILE_PX;
            if (!fogMap.isVisible(0, tx, ty)) continue;
        }

        const u8* sheet = get_unit_sheet(u.type, u.state);
        int frameIdx;
        bool hflip;
        if (is_animated_sheet(sheet, u.type)) {
            int base = DIR_TO_ANIM_BASE[u.direction];
            int anim = u.animFrame % 3;
            frameIdx = base + anim;
            hflip = DIR_HFLIP[u.direction];
        } else {
            frameIdx = DIR_TO_FRAME[u.direction];
            hflip = DIR_HFLIP[u.direction];
        }
        if (frameIdx >= SHEET_COLS * SHEET_ROWS) frameIdx = 0;

        u8 frame[CELL_W * CELL_H];
        extract_frame(sheet, frameIdx, frame);
        if (u.owner == 1) apply_color_remap(frame, CELL_W * CELL_H);
        if (u.state == USTATE_DEAD) {
            for (int j = 0; j < CELL_W * CELL_H; j++)
                if (frame[j] != 0) frame[j] = PAL_DARKGRAY;
        }

        // Selection highlight: draw 1px border around non-transparent area
        if (gs.selectedUnit == i) {
            for (int y = 0; y < CELL_H; y++)
                for (int x = 0; x < CELL_W; x++)
                    if (frame[y*CELL_W+x] != 0 &&
                        (x == 0 || y == 0 || x == CELL_W-1 || y == CELL_H-1))
                        frame[y*CELL_W+x] = PAL_WHITE;
        }

        blit_frame(buf, frame, CELL_W, CELL_H, sx, sy, hflip);

        // HP bar above unit
        draw_hp_bar(buf, sx + CELL_W / 2, sy - 3, 16, u.hp, playerUnitStats[u.owner][u.type].hp);
    }
}

// ---------------------------------------------------------------------------
// Minimal 4x5 bitmap font for build menu labels
// Each character stored as 5 rows of 4 bits (MSB = leftmost pixel)
// ---------------------------------------------------------------------------
static const u8 MINI_FONT[][5] = {
    // 'A'=0
    { 0b0110, 0b1001, 0b1111, 0b1001, 0b1001 },
    // 'B'=1
    { 0b1110, 0b1001, 0b1110, 0b1001, 0b1110 },
    // 'C'=2
    { 0b0111, 0b1000, 0b1000, 0b1000, 0b0111 },
    // 'F'=3
    { 0b1111, 0b1000, 0b1110, 0b1000, 0b1000 },
    // 'H'=4
    { 0b1001, 0b1001, 0b1111, 0b1001, 0b1001 },
    // 'K'=5
    { 0b1001, 0b1010, 0b1100, 0b1010, 0b1001 },
    // 'L'=6
    { 0b1000, 0b1000, 0b1000, 0b1000, 0b1111 },
    // 'M'=7
    { 0b1001, 0b1111, 0b1111, 0b1001, 0b1001 },
    // 'R'=8
    { 0b1110, 0b1001, 0b1110, 0b1010, 0b1001 },
    // 'S'=9
    { 0b0111, 0b1000, 0b0110, 0b0001, 0b1110 },
    // 'T'=10
    { 0b1111, 0b0110, 0b0110, 0b0110, 0b0110 },
    // 'c'=11
    { 0b0000, 0b0110, 0b1000, 0b1000, 0b0110 },
    // 'k'=12
    { 0b1000, 0b1010, 0b1100, 0b1010, 0b1001 },
    // 'm'=13
    { 0b0000, 0b1111, 0b1111, 0b1001, 0b1001 },
    // 's'=14
    { 0b0000, 0b0111, 0b0110, 0b0001, 0b1110 },
};

// Lookup: character to font index
static int font_idx(char ch) {
    switch(ch) {
        case 'A': return 0;  case 'B': return 1;  case 'C': return 2;
        case 'F': return 3;  case 'H': return 4;  case 'K': return 5;
        case 'L': return 6;  case 'M': return 7;  case 'R': return 8;
        case 'S': return 9;  case 'T': return 10; case 'c': return 11;
        case 'k': return 12; case 'm': return 13; case 's': return 14;
        default:  return -1;
    }
}

static void draw_mini_text(u8* vram, int x0, int y0, const char* str, u8 color) {
    int cx = x0;
    while (*str) {
        int fi = font_idx(*str);
        if (fi >= 0) {
            for (int row = 0; row < 5; row++) {
                int sy = y0 + row;
                if (sy < 0 || sy >= SCREEN_H) continue;
                for (int col = 0; col < 4; col++) {
                    int sx = cx + col;
                    if (sx < 0 || sx >= SCREEN_W) continue;
                    if (MINI_FONT[fi][row] & (0b1000 >> col)) {
                        vram[sy * 256 + sx] = color;
                    }
                }
            }
        }
        cx += 5; // 4px char + 1px gap
        str++;
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
        int textW = 2 * 5 - 1; // 2 chars × 5px - 1px gap = 9px
        int tx = ix + (BUILD_MENU_ITEM_W - textW) / 2;
        int ty = BUILD_MENU_Y + (BUILD_MENU_H - 5) / 2;
        draw_mini_text(vram, tx, ty, BLDG_ABBREV[i], textColor);

        // Border between items
        for (int y = BUILD_MENU_Y; y < SCREEN_H; y++) {
            int bx = ix + BUILD_MENU_ITEM_W - 1;
            if (bx < SCREEN_W) vram[y * 256 + bx] = PAL_BLACK;
        }
    }
}
