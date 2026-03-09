#include "ui.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "res_icons.h"
#include "iso.h"
#include "fog.h"
#include "tech.h"
#include "font.h"
#include <stdio.h>

// Access tech-modified stats
extern UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];

// Top screen: render to main RAM buffer, then DMA to VRAM to avoid flicker
static u16 topBuf[256 * 192] __attribute__((aligned(4)));
static u16* minimapVram = NULL;  // points to topBuf (all drawing goes here)
static u16* topVram = NULL;      // actual VRAM pointer (DMA target)
static int minimapBg = -1;

// Minimap dimensions: isometric diamond view
enum { MINIMAP_SX = 2, MINIMAP_SY = 1 };
enum { MINIMAP_W = (MAP_TILES * 2 - 1) * MINIMAP_SX + 2,  // ~126px
       MINIMAP_H = (MAP_TILES * 2 - 1) * MINIMAP_SY + 2 }; // ~63px
enum { STATUS_BAR_H = 16 };

// Layout: info panel on left, minimap on right
enum { INFO_X = 3, INFO_Y = 19, INFO_W = 119, INFO_H = 169 };
enum { DIVIDER_X = 125 };
enum { MINIMAP_X = 131, MINIMAP_Y = 22 };

// Textures (RGB15)
extern const u8 tex_wood_16_bin[];       // 16x16 dark wood tile
extern const u8 tex_parchment_64_bin[];  // 64x64 parchment tile
extern const u8 status_bar_bg_bin[];

// Text colors (RGB15 with alpha bit)
static const u16 COL_WHITE  = RGB15(31, 31, 31) | BIT(15);
static const u16 COL_GOLD   = RGB15(31, 28,  6) | BIT(15);
static const u16 COL_YELLOW = RGB15(31, 31,  0) | BIT(15);
static const u16 COL_RED    = RGB15(31,  4,  4) | BIT(15);
static const u16 COL_GREEN  = RGB15( 4, 28,  4) | BIT(15);
static const u16 COL_LGRAY  = RGB15(22, 22, 22) | BIT(15);
static const u16 COL_DGRAY  = RGB15( 8,  8,  8) | BIT(15);
static const u16 COL_BLACK  = RGB15( 0,  0,  0) | BIT(15);

// Bevel border colors
static const u16 BEVEL_LIGHT = RGB15(28, 26, 20) | BIT(15);
static const u16 BEVEL_DARK  = RGB15( 6,  4,  2) | BIT(15);

// Divider colors
static const u16 DIVIDER_GOLD = RGB15(26, 20,  6) | BIT(15);
static const u16 DIVIDER_RED  = RGB15(20,  4,  2) | BIT(15);

// ---------------------------------------------------------------------------
// Icon blitting
// ---------------------------------------------------------------------------
static void ui_blit_icon(int dx, int dy, const u16* icon, int w, int h) {
    if (!minimapVram) return;
    for (int y = 0; y < h; y++) {
        int sy = dy + y;
        if (sy < 0 || sy >= 192) continue;
        for (int x = 0; x < w; x++) {
            int sx = dx + x;
            if (sx < 0 || sx >= 256) continue;
            u16 px = icon[y * w + x];
            if (px != 0) minimapVram[sy * 256 + sx] = px;
        }
    }
}

// ---------------------------------------------------------------------------
// Texture tiling
// ---------------------------------------------------------------------------
static void ui_tile_rect(int dx, int dy, int w, int h,
                          const u16* tile, int tw, int th) {
    if (!minimapVram) return;
    for (int y = 0; y < h; y++) {
        int sy = dy + y;
        if (sy < 0 || sy >= 192) continue;
        int ty = y % th;
        for (int x = 0; x < w; x++) {
            int sx = dx + x;
            if (sx < 0 || sx >= 256) continue;
            minimapVram[sy * 256 + sx] = tile[ty * tw + (x % tw)];
        }
    }
}

// ---------------------------------------------------------------------------
// Beveled border (1px inset: light top/left, dark bottom/right)
// ---------------------------------------------------------------------------
static void ui_draw_bevel(int x, int y, int w, int h) {
    if (!minimapVram) return;
    // Top edge (light)
    for (int i = x; i < x + w && i < 256; i++)
        if (y >= 0 && y < 192) minimapVram[y * 256 + i] = BEVEL_LIGHT;
    // Left edge (light)
    for (int j = y; j < y + h && j < 192; j++)
        if (x >= 0 && x < 256) minimapVram[j * 256 + x] = BEVEL_LIGHT;
    // Bottom edge (dark)
    int by = y + h - 1;
    if (by >= 0 && by < 192)
        for (int i = x; i < x + w && i < 256; i++)
            minimapVram[by * 256 + i] = BEVEL_DARK;
    // Right edge (dark)
    int rx = x + w - 1;
    if (rx >= 0 && rx < 256)
        for (int j = y; j < y + h && j < 192; j++)
            minimapVram[j * 256 + rx] = BEVEL_DARK;
}

// ---------------------------------------------------------------------------
// HP bar (procedural colored bar)
// ---------------------------------------------------------------------------
static void ui_draw_hp_bar(int x, int y, int w, int h, int current, int maxHP) {
    if (!minimapVram || maxHP <= 0) return;

    // Dark border
    for (int j = y; j < y + h && j < 192; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < 256; i++) {
            if (i < 0) continue;
            if (j == y || j == y + h - 1 || i == x || i == x + w - 1)
                minimapVram[j * 256 + i] = RGB15(3, 3, 3) | BIT(15);
            else
                minimapVram[j * 256 + i] = RGB15(2, 2, 2) | BIT(15);
        }
    }

    // Fill proportionally
    int innerW = w - 2;
    int innerH = h - 2;
    int fillW = (current * innerW) / maxHP;
    if (fillW > innerW) fillW = innerW;
    if (fillW < 0) fillW = 0;

    int pct = (current * 100) / maxHP;
    u16 barColor;
    if (pct > 66) barColor = RGB15(4, 24, 4) | BIT(15);
    else if (pct > 33) barColor = RGB15(28, 28, 4) | BIT(15);
    else barColor = RGB15(28, 4, 4) | BIT(15);

    for (int j = y + 1; j < y + 1 + innerH && j < 192; j++) {
        if (j < 0) continue;
        for (int i = x + 1; i < x + 1 + fillW && i < 256; i++) {
            if (i < 0) continue;
            minimapVram[j * 256 + i] = barColor;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw the full panel background (called once per update)
// ---------------------------------------------------------------------------
static void ui_draw_panel_bg() {
    if (!minimapVram) return;
    const u16* woodTile = (const u16*)tex_wood_16_bin;
    const u16* parchTile = (const u16*)tex_parchment_64_bin;

    // Dark wood across entire area below status bar
    ui_tile_rect(0, STATUS_BAR_H, 256, 192 - STATUS_BAR_H, woodTile, 16, 16);

    // Parchment inset for info panel
    ui_tile_rect(INFO_X + 1, INFO_Y + 1, INFO_W - 2, INFO_H - 2, parchTile, 64, 64);

    // Bevel border around info panel
    ui_draw_bevel(INFO_X, INFO_Y, INFO_W, INFO_H);

    // Vertical divider (gold/red stripe)
    for (int y = STATUS_BAR_H; y < 192; y++) {
        minimapVram[y * 256 + DIVIDER_X]     = DIVIDER_RED;
        minimapVram[y * 256 + DIVIDER_X + 1] = DIVIDER_GOLD;
        minimapVram[y * 256 + DIVIDER_X + 2] = DIVIDER_RED;
    }
}

// ---------------------------------------------------------------------------
// Status bar (leather background + resource icons — unchanged)
// ---------------------------------------------------------------------------
static void ui_draw_status_bar(const GameState& gs) {
    if (!minimapVram) return;
    const Player& p = gs.players[0];

    const u16* bgTex = (const u16*)status_bar_bg_bin;
    for (int y = 0; y < STATUS_BAR_H; y++)
        for (int x = 0; x < 256; x++)
            minimapVram[y * 256 + x] = bgTex[y * 256 + x];

    int x = 4, y = (STATUS_BAR_H - RES_ICON_H) / 2;
    int textY = y + 1;

    ui_blit_icon(x, y, icon_food, ICON_FOOD_W, RES_ICON_H); x += ICON_FOOD_W + 1;
    x = font_draw_num_16(minimapVram, 256, 192, x, textY, p.resources[RES_FOOD], COL_WHITE, gameFont); x += 6;

    ui_blit_icon(x, y, icon_wood, ICON_WOOD_W, RES_ICON_H); x += ICON_WOOD_W + 1;
    x = font_draw_num_16(minimapVram, 256, 192, x, textY, p.resources[RES_WOOD], COL_WHITE, gameFont); x += 6;

    ui_blit_icon(x, y, icon_gold, ICON_GOLD_W, RES_ICON_H); x += ICON_GOLD_W + 1;
    x = font_draw_num_16(minimapVram, 256, 192, x, textY, p.resources[RES_GOLD], COL_WHITE, gameFont); x += 6;

    ui_blit_icon(x, y, icon_stone, ICON_STONE_W, RES_ICON_H); x += ICON_STONE_W + 1;
    x = font_draw_num_16(minimapVram, 256, 192, x, textY, p.resources[RES_STONE], COL_WHITE, gameFont);
}

// ---------------------------------------------------------------------------
// Minimap tile→pixel conversion
// ---------------------------------------------------------------------------
static inline void tileToMinimap(int tx, int ty, int& mx, int& my) {
    mx = MINIMAP_X + (tx - ty + MAP_TILES - 1) * MINIMAP_SX;
    my = MINIMAP_Y + (tx + ty) * MINIMAP_SY;
}

static u16 terrainMiniColors[TERRAIN_COUNT] = {
    RGB15( 8, 20,  8), // grass
    RGB15(20, 16,  8), // dirt
    RGB15( 4,  8, 24), // water
    RGB15( 2, 14,  2), // forest
    RGB15(28, 28,  4), // gold
    RGB15(16, 16, 16), // stone
    RGB15(12, 20,  4), // farm
    RGB15(24,  4, 12), // berries (reddish-pink)
};

// ---------------------------------------------------------------------------
// Init — pure bitmap, no console
// ---------------------------------------------------------------------------
void ui_init() {
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);

    // BG2: 16-bit bitmap — use mapBase 0 since no console needs VRAM_A anymore
    minimapBg = bgInit(2, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    topVram = (u16*)bgGetGfxPtr(minimapBg);
    minimapVram = topBuf;  // all drawing goes to main RAM buffer
    bgSetPriority(minimapBg, 0);
}

// ---------------------------------------------------------------------------
// Minimap drawing (same logic, but at new position)
// ---------------------------------------------------------------------------
static void ui_draw_minimap(const GameState& gs, const TerrainMap& terrain) {
    if (!minimapVram) return;

    // Clear minimap area to black (within the right panel region)
    u16 black = RGB15(0, 0, 0) | BIT(15);
    for (int my = MINIMAP_Y; my < MINIMAP_Y + MINIMAP_H && my < 192; my++) {
        for (int mx = MINIMAP_X; mx < MINIMAP_X + MINIMAP_W && mx < 256; mx++) {
            minimapVram[my * 256 + mx] = black;
        }
    }

    // Draw terrain
    for (int ty = 0; ty < MAP_TILES; ty++) {
        for (int tx = 0; tx < MAP_TILES; tx++) {
            u8 ttype = terrain.tileAt(tx, ty);
            u8 fogState = fogMap.state[0][ty][tx];

            u16 color;
            if (fogState == FOG_UNEXPLORED) {
                continue;
            } else {
                color = terrainMiniColors[ttype];
                if (fogState == FOG_EXPLORED) {
                    int r = (color & 0x1F) >> 1;
                    int g = ((color >> 5) & 0x1F) >> 1;
                    int b = ((color >> 10) & 0x1F) >> 1;
                    color = RGB15(r, g, b);
                }
            }
            color |= BIT(15);

            int mx, my;
            tileToMinimap(tx, ty, mx, my);

            for (int dy = 0; dy < MINIMAP_SY * 2; dy++) {
                int py = my + dy;
                if (py < 0 || py >= 192) continue;
                for (int dx = 0; dx < MINIMAP_SX * 2; dx++) {
                    int px = mx + dx;
                    if (px < 0 || px >= 256) continue;
                    minimapVram[py * 256 + px] = color;
                }
            }
        }
    }

    // Units on minimap
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD || units[i].state == USTATE_GARRISONED) continue;
        int tx = (units[i].x + TILE_PX/2) / TILE_PX;
        int ty = (units[i].y + TILE_PX/2) / TILE_PX;
        if (units[i].owner != 0 && !fogMap.isVisible(0, tx, ty)) continue;

        u16 dotColor = (units[i].owner == 0) ? RGB15(4, 8, 31) : RGB15(31, 4, 4);
        dotColor |= BIT(15);

        int mx, my;
        tileToMinimap(tx, ty, mx, my);
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                int px = mx + dx + 1;
                int py = my + dy + 1;
                if (px >= 0 && px < 256 && py >= 0 && py < 192)
                    minimapVram[py * 256 + px] = dotColor;
            }
        }
    }

    // Buildings on minimap
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        int tx = buildings[i].x / TILE_PX;
        int ty = buildings[i].y / TILE_PX;
        if (buildings[i].owner != 0 && !fogMap.isExplored(0, tx, ty)) continue;

        u16 dotColor = (buildings[i].owner == 0) ? RGB15(4, 8, 31) : RGB15(31, 4, 4);
        dotColor |= BIT(15);

        int tileW = BLDG_STATS[buildings[i].type].tileW;
        int tileH = BLDG_STATS[buildings[i].type].tileH;
        for (int bty = 0; bty < tileH; bty++) {
            for (int btx = 0; btx < tileW; btx++) {
                int mx, my;
                tileToMinimap(tx + btx, ty + bty, mx, my);
                for (int dy = 0; dy < MINIMAP_SY * 2; dy++) {
                    for (int dx = 0; dx < MINIMAP_SX * 2; dx++) {
                        int px = mx + dx;
                        int py = my + dy;
                        if (px >= 0 && px < 256 && py >= 0 && py < 192)
                            minimapVram[py * 256 + px] = dotColor;
                    }
                }
            }
        }
    }

    // Under attack alert — flashing red dot on minimap
    if (gs.underAttackTimer > 0 && gs.attackAlertTX >= 0 && ((gs.frameCount >> 3) & 1)) {
        int mx, my;
        tileToMinimap(gs.attackAlertTX, gs.attackAlertTY, mx, my);
        u16 alertColor = RGB15(31, 0, 0) | BIT(15);
        for (int dy = -1; dy <= 2; dy++) {
            for (int dx = -1; dx <= 2; dx++) {
                int px = mx + dx + 1;
                int py = my + dy + 1;
                if (px >= 0 && px < 256 && py >= 0 && py < 192)
                    minimapVram[py * 256 + px] = alertColor;
            }
        }
    }

    // Camera viewport outline
    int cTX[4], cTY[4];
    screenToTile(0, 0, gs.camX, gs.camY, cTX[0], cTY[0]);
    screenToTile(SCREEN_W, 0, gs.camX, gs.camY, cTX[1], cTY[1]);
    screenToTile(SCREEN_W, SCREEN_H, gs.camX, gs.camY, cTX[2], cTY[2]);
    screenToTile(0, SCREEN_H, gs.camX, gs.camY, cTX[3], cTY[3]);

    int cmx[4], cmy[4];
    for (int c = 0; c < 4; c++) {
        tileToMinimap(cTX[c], cTY[c], cmx[c], cmy[c]);
        cmx[c] += MINIMAP_SX;
        cmy[c] += MINIMAP_SY;
    }

    u16 white = RGB15(31, 31, 31) | BIT(15);
    for (int edge = 0; edge < 4; edge++) {
        int x0 = cmx[edge], y0 = cmy[edge];
        int x1 = cmx[(edge + 1) % 4], y1 = cmy[(edge + 1) % 4];
        int dx = x1 - x0, dy = y1 - y0;
        int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
        dx = dx * sx; dy = dy * sy;
        int err = dx - dy;
        while (true) {
            if (x0 >= 0 && x0 < 256 && y0 >= 0 && y0 < 192)
                minimapVram[y0 * 256 + x0] = white;
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
    }
}

// ---------------------------------------------------------------------------
// Info panel (left side, parchment background)
// ---------------------------------------------------------------------------
static void ui_draw_info_panel(const GameState& gs, const TerrainMap& terrain) {
    if (!minimapVram) return;

    // Text area within the parchment inset (inside bevel)
    const int TX = INFO_X + 4;   // text start X
    int ty = INFO_Y + 3;         // text start Y, advances as we draw
    const int TW = INFO_W - 8;   // text area width
    const int TX_END = TX + TW;
    (void)TX_END;

    // Re-tile parchment to clear old text each frame
    const u16* parchTile = (const u16*)tex_parchment_64_bin;
    ui_tile_rect(INFO_X + 1, INFO_Y + 1, INFO_W - 2, INFO_H - 2, parchTile, 64, 64);

    const Player& p0 = gs.players[0];

    // Dark brown text color for parchment readability
    u16 colText = RGB15(6, 4, 2) | BIT(15);
    u16 colGold = RGB15(20, 16, 2) | BIT(15);
    u16 colRed  = RGB15(24, 4, 2) | BIT(15);
    u16 colYellow = RGB15(24, 20, 0) | BIT(15);
    u16 colBlue = RGB15(2, 4, 24) | BIT(15);

    // --- Always-shown info ---

    // Population
    int x = TX;
    x = font_draw_str_16(minimapVram, 256, 192, x, ty, "Pop:", colText, gameFont);
    x = font_draw_num_16(minimapVram, 256, 192, x, ty, p0.popCount, colText, gameFont);
    x = font_draw_str_16(minimapVram, 256, 192, x, ty, "/", colText, gameFont);
    font_draw_num_16(minimapVram, 256, 192, x, ty, p0.popCap, colText, gameFont);
    ty += 12;

    // Age
    static const char* AGE_SHORT[] = { "Dark", "Feudal", "Castle", "Imperial" };
    x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Age:", colText, gameFont);
    font_draw_str_16(minimapVram, 256, 192, x, ty, AGE_SHORT[p0.age], colGold, gameFont);
    ty += 12;

    // Age-up progress
    if (p0.ageProgress >= 0) {
        int nextAge = p0.age + 1;
        if (nextAge < AGE_COUNT) {
            int pct = (p0.ageProgress * 100) / AGE_RESEARCH_TIME[nextAge];
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Advancing:", colGold, gameFont);
            x = font_draw_num_16(minimapVram, 256, 192, x, ty, pct, colGold, gameFont);
            font_draw_str_16(minimapVram, 256, 192, x, ty, "%", colGold, gameFont);
            ty += 12;
        }
    }

    // Scores
    {
        int scores[NUM_PLAYERS] = {0, 0};
        for (int p = 0; p < NUM_PLAYERS; p++) {
            for (int r = 0; r < RES_COUNT; r++)
                scores[p] += gs.players[p].resources[r];
            scores[p] += gs.players[p].age * 500;
            for (int i = 0; i < MAX_UNITS; i++) {
                if (units[i].alive && units[i].owner == p && units[i].state != USTATE_DEAD)
                    scores[p] += 50;
            }
            for (int i = 0; i < MAX_BUILDINGS; i++) {
                if (buildings[i].alive && buildings[i].owner == p)
                    scores[p] += 100;
            }
        }
        x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "You:", colBlue, gameFont);
        font_draw_num_16(minimapVram, 256, 192, x, ty, scores[0], colText, gameFont);
        ty += 12;
        x = font_draw_str_16(minimapVram, 256, 192, TX, ty, " AI:", colRed, gameFont);
        font_draw_num_16(minimapVram, 256, 192, x, ty, scores[1], colText, gameFont);
        ty += 12;
    }

    // Idle villager warning
    {
        int idleVils = 0;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (units[i].alive && units[i].owner == 0 &&
                units[i].type == UNIT_VILLAGER && units[i].state == USTATE_IDLE)
                idleVils++;
        }
        if (idleVils > 0) {
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "!", colYellow, gameFont);
            x = font_draw_num_16(minimapVram, 256, 192, x, ty, idleVils, colYellow, gameFont);
            font_draw_str_16(minimapVram, 256, 192, x, ty, " idle", colYellow, gameFont);
            ty += 12;
        }
    }

    // Under attack alert
    if (gs.underAttackTimer > 0 && ((gs.frameCount >> 3) & 1)) {
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "UNDER ATTACK!", colRed, gameFont);
        ty += 12;
    }

    // --- Separator line ---
    ty += 2;
    for (int i = TX; i < TX + TW && i < 256; i++) {
        if (ty >= 0 && ty < 192)
            minimapVram[ty * 256 + i] = RGB15(16, 14, 10) | BIT(15);
    }
    ty += 4;

    // --- Context-dependent info ---

    if (gs.phase == PHASE_VICTORY) {
        font_draw_str_16(minimapVram, 256, 192, TX + 10, ty, "VICTORY!", colGold, gameFont);
        ty += 16;
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "Kills:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, TX + 50, ty, gs.unitsKilled[0], colText, gameFont);
        ty += 12;
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "Lost:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, TX + 50, ty, gs.unitsLost[0], colText, gameFont);
        ty += 12;
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "Razed:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, TX + 50, ty, gs.bldgsDestroyed[0], colText, gameFont);
        return;
    } else if (gs.phase == PHASE_DEFEAT) {
        font_draw_str_16(minimapVram, 256, 192, TX + 10, ty, "DEFEAT!", colRed, gameFont);
        ty += 16;
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "Kills:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, TX + 50, ty, gs.unitsKilled[0], colText, gameFont);
        ty += 12;
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "Lost:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, TX + 50, ty, gs.unitsLost[0], colText, gameFont);
        ty += 12;
        font_draw_str_16(minimapVram, 256, 192, TX, ty, "Razed:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, TX + 50, ty, gs.bldgsDestroyed[0], colText, gameFont);
        return;
    }

    if (gs.selectionCount > 1) {
        // Multi-selection
        x = font_draw_num_16(minimapVram, 256, 192, TX, ty, gs.selectionCount, colText, gameFont);
        font_draw_str_16(minimapVram, 256, 192, x, ty, " selected", colText, gameFont);
        ty += 12;

        int vilCount = 0, milCount = 0;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!gs.unitSelected[i] || !units[i].alive) continue;
            if (units[i].type == UNIT_VILLAGER) vilCount++;
            else milCount++;
        }
        if (vilCount > 0) {
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Vils:", colText, gameFont);
            font_draw_num_16(minimapVram, 256, 192, x, ty, vilCount, colText, gameFont);
            ty += 12;
        }
        if (milCount > 0) {
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Military:", colText, gameFont);
            font_draw_num_16(minimapVram, 256, 192, x, ty, milCount, colText, gameFont);
            ty += 12;
        }

    } else if (gs.selectedUnit >= 0 && gs.selectedUnit < MAX_UNITS && units[gs.selectedUnit].alive) {
        const Unit& u = units[gs.selectedUnit];
        const UnitStats& st = playerUnitStats[u.owner][u.type];

        // Unit name
        font_draw_str_16(minimapVram, 256, 192, TX, ty, UNIT_NAMES[u.type], colText, gameFont);
        ty += 12;

        // State
        const char* stateStr = "";
        switch (u.state) {
        case USTATE_IDLE:      stateStr = "Idle"; break;
        case USTATE_MOVING:    stateStr = (u.patrolAX >= 0) ? "Patrol" : "Moving"; break;
        case USTATE_GATHERING: stateStr = "Gathering"; break;
        case USTATE_RETURNING: stateStr = "Returning"; break;
        case USTATE_BUILDING:  stateStr = "Building"; break;
        case USTATE_ATTACKING: stateStr = "Fighting"; break;
        case USTATE_SCOUTING:  stateStr = "Scouting"; break;
        default: break;
        }
        font_draw_str_16(minimapVram, 256, 192, TX + 4, ty, stateStr, colGold, gameFont);
        // Show stance for military units
        if (u.type != UNIT_VILLAGER && u.type != UNIT_SHEEP) {
            const char* stanceStr = "";
            switch (u.stance) {
            case STANCE_AGGRESSIVE: stanceStr = " [Aggr]"; break;
            case STANCE_DEFENSIVE:  stanceStr = " [Def]"; break;
            case STANCE_STAND:      stanceStr = " [Stand]"; break;
            case STANCE_NO_ATTACK:  stanceStr = " [NoAtk]"; break;
            }
            int sx = TX + 4 + 8 * 8; // after state text
            font_draw_str_16(minimapVram, 256, 192, sx, ty, stanceStr, colText, gameFont);
        }
        ty += 14;

        // HP bar
        ui_draw_hp_bar(TX, ty, 80, 6, u.hp, st.hp);
        x = font_draw_str_16(minimapVram, 256, 192, TX + 84, ty - 1, "", colText, gameFont);
        x = font_draw_num_16(minimapVram, 256, 192, TX + 84, ty - 1, u.hp, colText, gameFont);
        x = font_draw_str_16(minimapVram, 256, 192, x, ty - 1, "/", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, x, ty - 1, st.hp, colText, gameFont);
        ty += 10;

        // ATK / ARM — show primary attack class and melee+pierce armor
        int mainAtk = st.attack[DMG_MELEE] + st.attack[DMG_PIERCE];
        int mainArm = st.armor[DMG_MELEE] + st.armor[DMG_PIERCE];
        x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "ATK:", colText, gameFont);
        x = font_draw_num_16(minimapVram, 256, 192, x, ty, mainAtk, colText, gameFont);
        x = font_draw_str_16(minimapVram, 256, 192, x + 4, ty, "ARM:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, x, ty, mainArm, colText, gameFont);
        ty += 12;

        // RNG / SPD
        x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "RNG:", colText, gameFont);
        x = font_draw_num_16(minimapVram, 256, 192, x, ty, st.range, colText, gameFont);
        x = font_draw_str_16(minimapVram, 256, 192, x + 4, ty, "SPD:", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, x, ty, st.speed, colText, gameFont);
        ty += 12;

        // Carry info
        if (u.carryAmount > 0) {
            const char* resNames[] = {"Food","Wood","Gold","Stone"};
            if (u.carryType < RES_COUNT) {
                x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Carry:", colText, gameFont);
                x = font_draw_num_16(minimapVram, 256, 192, x, ty, u.carryAmount, colText, gameFont);
                x = font_draw_str_16(minimapVram, 256, 192, x, ty, " ", colText, gameFont);
                font_draw_str_16(minimapVram, 256, 192, x, ty, resNames[u.carryType], colText, gameFont);
            }
        }

    } else if (gs.selectedBldg >= 0 && gs.selectedBldg < MAX_BUILDINGS && buildings[gs.selectedBldg].alive) {
        const Building& b = buildings[gs.selectedBldg];
        const BuildingStats& st = BLDG_STATS[b.type];

        // Building name
        font_draw_str_16(minimapVram, 256, 192, TX, ty, BLDG_NAMES[b.type], colText, gameFont);
        ty += 12;

        // Garrison info
        if (b.garrisonCount > 0) {
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "[", colText, gameFont);
            x = font_draw_num_16(minimapVram, 256, 192, x, ty, b.garrisonCount, colText, gameFont);
            font_draw_str_16(minimapVram, 256, 192, x, ty, " inside]", colText, gameFont);
            ty += 12;
        }

        // HP bar
        ui_draw_hp_bar(TX, ty, 80, 6, b.hp, st.hp);
        x = font_draw_num_16(minimapVram, 256, 192, TX + 84, ty - 1, b.hp, colText, gameFont);
        x = font_draw_str_16(minimapVram, 256, 192, x, ty - 1, "/", colText, gameFont);
        font_draw_num_16(minimapVram, 256, 192, x, ty - 1, st.hp, colText, gameFont);
        ty += 10;

        // Build progress or train info
        if (b.buildProgress < st.buildTime) {
            int pct = (b.buildProgress * 100) / st.buildTime;
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Build:", colText, gameFont);
            x = font_draw_num_16(minimapVram, 256, 192, x, ty, pct, colText, gameFont);
            font_draw_str_16(minimapVram, 256, 192, x, ty, "%", colText, gameFont);
            ty += 12;
        } else if (b.type == BLDG_MARKET) {
            static const char* TRADE_SHORT[4] = {
                "100F->70G", "100W->70F", "100G->70S", "100S->70W"
            };
            font_draw_str_16(minimapVram, 256, 192, TX, ty, TRADE_SHORT[gs.marketTradeIdx], colGold, gameFont);
            ty += 12;
            font_draw_str_16(minimapVram, 256, 192, TX, ty, "L/R:Cycle", colText, gameFont);
            ty += 12;
            font_draw_str_16(minimapVram, 256, 192, TX, ty, "START:Trade", colText, gameFont);
            ty += 12;
        } else {
            if (b.trainQueue[0] >= 0) {
                int pct = (b.trainProgress * 100) / UNIT_STATS[(u8)b.trainQueue[0]].trainTime;
                x = font_draw_str_16(minimapVram, 256, 192, TX, ty, UNIT_NAMES[(u8)b.trainQueue[0]], colGold, gameFont);
                x = font_draw_str_16(minimapVram, 256, 192, x, ty, ":", colGold, gameFont);
                x = font_draw_num_16(minimapVram, 256, 192, x, ty, pct, colGold, gameFont);
                font_draw_str_16(minimapVram, 256, 192, x, ty, "%", colGold, gameFont);
                ty += 12;

                if (b.trainQueue[1] >= 0) {
                    x = font_draw_str_16(minimapVram, 256, 192, TX, ty, "Q:", colText, gameFont);
                    x = font_draw_str_16(minimapVram, 256, 192, x, ty, UNIT_NAMES[(u8)b.trainQueue[1]], colText, gameFont);
                    if (b.trainQueue[2] >= 0) {
                        x = font_draw_str_16(minimapVram, 256, 192, x, ty, ",", colText, gameFont);
                        font_draw_str_16(minimapVram, 256, 192, x, ty, UNIT_NAMES[(u8)b.trainQueue[2]], colText, gameFont);
                    }
                    ty += 12;
                }
            } else {
                font_draw_str_16(minimapVram, 256, 192, TX, ty, "[Idle]", colText, gameFont);
                ty += 12;
            }
        }

        // TC age-up option
        if (b.type == BLDG_TOWN_CENTER && gs.players[0].ageProgress < 0 &&
            gs.players[0].age < AGE_IMPERIAL && ty + 12 < INFO_Y + INFO_H) {
            int na = gs.players[0].age + 1;
            char buf[32];
            snprintf(buf, sizeof(buf), "START:%s", AGE_NAMES[na]);
            font_draw_str_16(minimapVram, 256, 192, TX, ty, buf, colGold, gameFont);
            ty += 12;
        }

        // Techs
        for (int t = 0; t < TECH_COUNT && ty + 12 < INFO_Y + INFO_H; t++) {
            if (TECH_TABLE[t].bldgReq == b.type &&
                !tech_is_researched(gs, 0, t) &&
                gs.players[0].age >= TECH_TABLE[t].ageReq) {
                font_draw_str_16(minimapVram, 256, 192, TX, ty, TECH_TABLE[t].name, colGold, gameFont);
                ty += 12;
            }
        }

        // Trainable units (highlight selected)
        for (int ut = 0; ut < UNIT_TYPE_COUNT && ty + 12 < INFO_Y + INFO_H; ut++) {
            if (UNIT_STATS[ut].bldgReq == b.type && gs.players[0].age >= UNIT_STATS[ut].ageReq) {
                bool isSelected = (ut == gs.trainUnitType);
                char buf[32];
                snprintf(buf, sizeof(buf), "%s%s F%d W%d G%d",
                         isSelected ? ">" : " ", UNIT_NAMES[ut],
                         UNIT_STATS[ut].cost[RES_FOOD], UNIT_STATS[ut].cost[RES_WOOD],
                         UNIT_STATS[ut].cost[RES_GOLD]);
                font_draw_str_16(minimapVram, 256, 192, TX, ty, buf,
                                 isSelected ? colGold : colText, gameFont);
                ty += 12;
            }
        }

        // Rally point indicator
        if (b.rallyTX >= 0 && b.rallyTY >= 0 && ty + 12 < INFO_Y + INFO_H) {
            font_draw_str_16(minimapVram, 256, 192, TX, ty, "Rally set", colGold, gameFont);
            ty += 12;
        }

    } else if (gs.selectedTileX >= 0 && gs.selectedTileX < MAP_TILES &&
               gs.selectedTileY >= 0 && gs.selectedTileY < MAP_TILES) {
        u8 tt = terrain.tileAt(gs.selectedTileX, gs.selectedTileY);
        int amt = terrain.resourceAmt[gs.selectedTileY][gs.selectedTileX];
        const char* tileName = "Grass";
        const char* resName = "";
        switch (tt) {
        case TERRAIN_FOREST: tileName = "Forest"; resName = "Wood"; break;
        case TERRAIN_GOLD:   tileName = "Gold Mine"; resName = "Gold"; break;
        case TERRAIN_STONE:  tileName = "Stone Mine"; resName = "Stone"; break;
        case TERRAIN_FARM:   tileName = "Farm"; resName = "Food"; break;
        case TERRAIN_BERRIES: tileName = "Berries"; resName = "Food"; break;
        default: break;
        }
        font_draw_str_16(minimapVram, 256, 192, TX, ty, tileName, colText, gameFont);
        ty += 12;
        if (amt > 0) {
            x = font_draw_str_16(minimapVram, 256, 192, TX, ty, resName, colText, gameFont);
            x = font_draw_str_16(minimapVram, 256, 192, x, ty, ":", colText, gameFont);
            font_draw_num_16(minimapVram, 256, 192, x, ty, amt, colText, gameFont);
        }
    }
}

// ---------------------------------------------------------------------------
// Controls area (below minimap, right panel)
// ---------------------------------------------------------------------------
static void ui_draw_controls(const GameState& gs) {
    if (!minimapVram) return;

    // Position below minimap diamond
    int cy = MINIMAP_Y + MINIMAP_H + 8;
    int cx = MINIMAP_X + 2;
    u16 colCtrl = RGB15(24, 22, 18) | BIT(15);  // light text on dark wood

    if (gs.phase == PHASE_VICTORY) {
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "VICTORY!", COL_GOLD, gameFont);
        cy += 12;
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "Touch:restart", colCtrl, gameFont);
        return;
    } else if (gs.phase == PHASE_DEFEAT) {
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "DEFEAT!", COL_RED, gameFont);
        cy += 12;
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "Touch:restart", colCtrl, gameFont);
        return;
    }

    if (gs.buildMenuOpen) {
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "[Build Menu]", colCtrl, gameFont);
    } else if (gs.inputMode == 1) {
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "Place:", colCtrl, gameFont);
        int nx = font_draw_str_16(minimapVram, 256, 192, cx + 40, cy, BLDG_NAMES[gs.placeBldgType], colCtrl, gameFont);
        (void)nx;
        cy += 12;
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "B:Cancel", colCtrl, gameFont);
    } else if (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
               buildings[gs.selectedBldg].type == BLDG_TOWN_CENTER &&
               buildings[gs.selectedBldg].garrisonCount > 0) {
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "A:Ungarrison", colCtrl, gameFont);
        cy += 12;
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "START:Train", colCtrl, gameFont);
    } else {
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "X:Build", colCtrl, gameFont);
        cy += 12;
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "L/R:Cycle Vil", colCtrl, gameFont);
        cy += 12;
        font_draw_str_16(minimapVram, 256, 192, cx, cy, "B:Cancel", colCtrl, gameFont);
    }
}

// ---------------------------------------------------------------------------
// Main update
// ---------------------------------------------------------------------------
void ui_update(const GameState& gs, const TerrainMap& terrain) {
    // Draw everything every 4th frame for performance
    if ((gs.frameCount & 3) == 0) {
        ui_draw_status_bar(gs);
        ui_draw_panel_bg();
        ui_draw_minimap(gs, terrain);
        ui_draw_info_panel(gs, terrain);
        ui_draw_controls(gs);

        // DMA copy buffer to VRAM (avoids flicker from mid-scanline writes)
        DC_FlushRange(topBuf, 256 * 192 * 2);
        dmaCopy(topBuf, topVram, 256 * 192 * 2);
    }
}
