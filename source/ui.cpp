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

// Top screen: VRAM_A used as bitmap for minimap
// Console used for text overlay

static PrintConsole topConsole;
static u16* minimapVram = NULL;
static int minimapBg = -1;

// Minimap dimensions: isometric diamond view
// Uses per-pixel reverse iso projection to map minimap pixels to tiles
// X scale = 2px, Y scale = 1px to match the 2:1 isometric tile ratio (32×16)
enum { MINIMAP_SX = 2, MINIMAP_SY = 1 };
enum { MINIMAP_W = (MAP_TILES * 2 - 1) * MINIMAP_SX + 2,  // ~126px
       MINIMAP_H = (MAP_TILES * 2 - 1) * MINIMAP_SY + 2 }; // ~63px
enum { STATUS_BAR_H = 16 };
enum { MINIMAP_X = 0, MINIMAP_Y = STATUS_BAR_H };

static void ui_blit_icon(int dx, int dy, const u16* icon) {
    if (!minimapVram) return;
    for (int y = 0; y < 8; y++) {
        int sy = dy + y;
        if (sy < 0 || sy >= 192) continue;
        for (int x = 0; x < 8; x++) {
            int sx = dx + x;
            if (sx < 0 || sx >= 256) continue;
            u16 px = icon[y * 8 + x];
            if (px != 0) minimapVram[sy * 256 + sx] = px;
        }
    }
}

// ---------------------------------------------------------------------------
// Status bar drawing using AoE2 bitmap font
// ---------------------------------------------------------------------------
static void ui_draw_status_bar(const GameState& gs) {
    if (!minimapVram) return;
    const Player& p = gs.players[0];

    u16 bgColor = RGB15(3, 2, 1) | BIT(15);
    for (int y = 0; y < STATUS_BAR_H; y++)
        for (int x = 0; x < 256; x++)
            minimapVram[y * 256 + x] = bgColor;

    u16 white  = RGB15(31, 31, 31) | BIT(15);
    u16 gray   = RGB15(16, 16, 16) | BIT(15);

    int x = 2, y = 1;

    // Food: icon + number
    ui_blit_icon(x, y, icon_food); x += 10;
    x = font_draw_num_16(minimapVram, 256, 192, x, y, p.resources[RES_FOOD], white, gameFont); x += 5;

    // Wood: icon + number
    ui_blit_icon(x, y, icon_wood); x += 10;
    x = font_draw_num_16(minimapVram, 256, 192, x, y, p.resources[RES_WOOD], white, gameFont); x += 5;

    // Gold: icon + number
    ui_blit_icon(x, y, icon_gold); x += 10;
    x = font_draw_num_16(minimapVram, 256, 192, x, y, p.resources[RES_GOLD], white, gameFont); x += 5;

    // Stone: icon + number
    ui_blit_icon(x, y, icon_stone); x += 10;
    x = font_draw_num_16(minimapVram, 256, 192, x, y, p.resources[RES_STONE], white, gameFont); x += 7;

    // Population: icon + count/cap
    ui_blit_icon(x, y, icon_pop); x += 10;
    x = font_draw_num_16(minimapVram, 256, 192, x, y, p.popCount, white, gameFont);
    x = font_draw_str_16(minimapVram, 256, 192, x, y, "/", gray, gameFont);
    x = font_draw_num_16(minimapVram, 256, 192, x, y, p.popCap, white, gameFont);

    // Idle villager count (yellow warning if any)
    int idleVils = 0;
    for (int i = 0; i < MAX_UNITS; i++) {
        if (units[i].alive && units[i].owner == 0 &&
            units[i].type == UNIT_VILLAGER && units[i].state == USTATE_IDLE)
            idleVils++;
    }
    if (idleVils > 0) {
        x += 5;
        u16 yellow = RGB15(31, 28, 4) | BIT(15);
        x = font_draw_str_16(minimapVram, 256, 192, x, y, "!", yellow, gameFont);
        x = font_draw_num_16(minimapVram, 256, 192, x, y, idleVils, yellow, gameFont);
    }

    // Age indicator (right-aligned)
    static const char* AGE_SHORT[] = { "I", "II", "III", "IV" };
    u16 gold = RGB15(31, 24, 0) | BIT(15);
    int ageW = font_string_width(gameFont, AGE_SHORT[p.age]);
    font_draw_str_16(minimapVram, 256, 192, 254 - ageW, y, AGE_SHORT[p.age], gold, gameFont);
}

// Convert tile coords to minimap pixel coords (isometric projection)
static inline void tileToMinimap(int tx, int ty, int& mx, int& my) {
    mx = MINIMAP_X + (tx - ty + MAP_TILES - 1) * MINIMAP_SX;
    my = MINIMAP_Y + (tx + ty) * MINIMAP_SY;
}

// Minimap colors (RGB15)
static u16 terrainMiniColors[TERRAIN_COUNT] = {
    RGB15( 8, 20,  8), // grass
    RGB15(20, 16,  8), // dirt
    RGB15( 4,  8, 24), // water
    RGB15( 2, 14,  2), // forest
    RGB15(28, 28,  4), // gold
    RGB15(16, 16, 16), // stone
    RGB15(12, 20,  4), // farm
};

void ui_init() {
    // Setup top screen for bitmap + console
    // Main engine: mode 5 for bitmap BG + console
    videoSetMode(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);

    // BG2: 16-bit bitmap for minimap — in VRAM_B (mapBase 8 = offset 0x20000)
    // VRAM_A (offset 0) is reserved for console tiles/map
    minimapBg = bgInit(2, BgType_Bmp16, BgSize_B16_256x256, 8, 0);
    minimapVram = (u16*)bgGetGfxPtr(minimapBg);
    bgSetPriority(minimapBg, 1);

    // BG0: console text (higher priority = on top)
    consoleInit(&topConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 0, true, true);
    bgSetPriority(topConsole.bgId, 0);

    consoleSelect(&topConsole);
}

static void ui_draw_minimap(const GameState& gs, const TerrainMap& terrain) {
    if (!minimapVram) return;

    // Clear minimap area to black (start at MINIMAP_Y to preserve status bar)
    u16 black = RGB15(0, 0, 0) | BIT(15);
    for (int my = MINIMAP_Y; my < MINIMAP_Y + MINIMAP_H && my < 192; my++) {
        for (int mx = 0; mx < MINIMAP_W && mx < 256; mx++) {
            minimapVram[my * 256 + mx] = black;
        }
    }

    // Draw terrain as isometric diamond — iterate tiles and fill minimap pixels
    for (int ty = 0; ty < MAP_TILES; ty++) {
        for (int tx = 0; tx < MAP_TILES; tx++) {
            u8 ttype = terrain.tileAt(tx, ty);
            u8 fogState = fogMap.state[0][ty][tx];

            u16 color;
            if (fogState == FOG_UNEXPLORED) {
                continue; // leave as black
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

            // Map tile to minimap position and fill a small diamond
            int mx, my;
            tileToMinimap(tx, ty, mx, my);

            // Draw a small diamond matching the iso aspect ratio (2px wide × 1px tall)
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

    // Draw units on minimap
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD || units[i].state == USTATE_GARRISONED) continue;
        int tx = (units[i].x + TILE_PX/2) / TILE_PX;
        int ty = (units[i].y + TILE_PX/2) / TILE_PX;

        if (units[i].owner != 0 && !fogMap.isVisible(0, tx, ty)) continue;

        u16 dotColor = (units[i].owner == 0) ? RGB15(4, 8, 31) : RGB15(31, 4, 4);
        dotColor |= BIT(15);

        int mx, my;
        tileToMinimap(tx, ty, mx, my);
        // Draw 2x2 dot
        for (int dy = 0; dy < 2; dy++) {
            for (int dx = 0; dx < 2; dx++) {
                int px = mx + dx + 1;
                int py = my + dy + 1;
                if (px >= 0 && px < 256 && py >= 0 && py < 192) {
                    minimapVram[py * 256 + px] = dotColor;
                }
            }
        }
    }

    // Draw buildings on minimap
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        int tx = buildings[i].x / TILE_PX;
        int ty = buildings[i].y / TILE_PX;

        if (buildings[i].owner != 0 && !fogMap.isExplored(0, tx, ty)) continue;

        u16 dotColor = (buildings[i].owner == 0) ? RGB15(4, 8, 31) : RGB15(31, 4, 4);
        dotColor |= BIT(15);

        int tileW = BLDG_STATS[buildings[i].type].tileW;
        int tileH = BLDG_STATS[buildings[i].type].tileH;

        // Fill all tiles of the building
        for (int bty = 0; bty < tileH; bty++) {
            for (int btx = 0; btx < tileW; btx++) {
                int mx, my;
                tileToMinimap(tx + btx, ty + bty, mx, my);
                for (int dy = 0; dy < MINIMAP_SY * 2; dy++) {
                    for (int dx = 0; dx < MINIMAP_SX * 2; dx++) {
                        int px = mx + dx;
                        int py = my + dy;
                        if (px >= 0 && px < 256 && py >= 0 && py < 192) {
                            minimapVram[py * 256 + px] = dotColor;
                        }
                    }
                }
            }
        }
    }

    // Draw camera viewport outline as rectangle matching the screen
    // Screen corners map to tile coords, then to minimap coords — forms a rectangle
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

    // Draw lines between the 4 diamond vertices (Bresenham)
    for (int edge = 0; edge < 4; edge++) {
        int x0 = cmx[edge], y0 = cmy[edge];
        int x1 = cmx[(edge + 1) % 4], y1 = cmy[(edge + 1) % 4];
        int dx = x1 - x0, dy = y1 - y0;
        int sx = (dx > 0) ? 1 : -1, sy = (dy > 0) ? 1 : -1;
        dx = dx * sx; dy = dy * sy;
        int err = dx - dy;
        while (true) {
            if (x0 >= 0 && x0 < 256 && y0 >= 0 && y0 < 192) {
                minimapVram[y0 * 256 + x0] = white;
            }
            if (x0 == x1 && y0 == y1) break;
            int e2 = 2 * err;
            if (e2 > -dy) { err -= dy; x0 += sx; }
            if (e2 < dx)  { err += dx; y0 += sy; }
        }
    }
}

void ui_update(const GameState& gs, const TerrainMap& terrain) {
    consoleSelect(&topConsole);

    // Draw minimap (only every 4th frame for performance)
    if ((gs.frameCount & 3) == 0) {
        ui_draw_status_bar(gs);
        ui_draw_minimap(gs, terrain);
    }

    // Clear console area (below minimap)
    // Console is overlaid on the bitmap, so we just print text
    consoleClear();

    const Player& p0 = gs.players[0];

    // Age progress (only shown when advancing)
    if (p0.ageProgress >= 0) {
        int nextAge = p0.age + 1;
        if (nextAge < AGE_COUNT) {
            int pct = (p0.ageProgress * 100) / AGE_RESEARCH_TIME[nextAge];
            iprintf("\x1b[0;17HAge Up:%d%%", pct);
        }
    }

    // Selected unit info (below minimap area)
    if (gs.selectionCount > 1) {
        // Multi-selection: show count summary
        int vilCount = 0, milCount = 0;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!gs.unitSelected[i] || !units[i].alive) continue;
            if (units[i].type == UNIT_VILLAGER) vilCount++;
            else milCount++;
        }
        iprintf("\x1b[17;0H %d units selected", gs.selectionCount);
        if (vilCount > 0) iprintf("\x1b[18;0H  Villagers: %d", vilCount);
        if (milCount > 0) iprintf("\x1b[19;0H  Military: %d", milCount);
    } else if (gs.selectedUnit >= 0 && gs.selectedUnit < MAX_UNITS && units[gs.selectedUnit].alive) {
        const Unit& u = units[gs.selectedUnit];
        const UnitStats& st = playerUnitStats[u.owner][u.type];
        // Unit name + state
        const char* stateStr = "";
        switch (u.state) {
        case USTATE_IDLE:      stateStr = "Idle"; break;
        case USTATE_MOVING:    stateStr = "Moving"; break;
        case USTATE_GATHERING: stateStr = "Gathering"; break;
        case USTATE_RETURNING: stateStr = "Returning"; break;
        case USTATE_BUILDING:  stateStr = "Building"; break;
        case USTATE_ATTACKING: stateStr = "Fighting"; break;
        case USTATE_SCOUTING:  stateStr = "Scouting"; break;
        default: break;
        }
        iprintf("\x1b[17;0H %s - %s", UNIT_NAMES[u.type], stateStr);
        iprintf("\x1b[18;0H HP:%d/%d ATK:%d ARM:%d", u.hp, st.hp, st.attack, st.armor);
        iprintf("\x1b[19;0H RNG:%d SPD:%d", st.range, st.speed);
        if (u.carryAmount > 0) {
            const char* resNames[] = {"Food","Wood","Gold","Stone"};
            if (u.carryType < RES_COUNT) {
                iprintf("\x1b[20;0H Carry:%d %s", u.carryAmount, resNames[u.carryType]);
            }
        }
    } else if (gs.selectedBldg >= 0 && gs.selectedBldg < MAX_BUILDINGS && buildings[gs.selectedBldg].alive) {
        const Building& b = buildings[gs.selectedBldg];
        const BuildingStats& st = BLDG_STATS[b.type];
        iprintf("\x1b[17;0H %s", BLDG_NAMES[b.type]);
        if (b.garrisonCount > 0) {
            iprintf(" [%d inside]", b.garrisonCount);
        }
        iprintf("\x1b[18;0H HP:%d/%d", b.hp, st.hp);

        if (b.buildProgress < st.buildTime) {
            int pct = (b.buildProgress * 100) / st.buildTime;
            iprintf("\x1b[19;0H Building:%d%%", pct);
        } else {
            if (b.trainQueue[0] >= 0) {
                int pct = (b.trainProgress * 100) / UNIT_STATS[(u8)b.trainQueue[0]].trainTime;
                iprintf("\x1b[19;0H Train %s:%d%%",
                        UNIT_NAMES[(u8)b.trainQueue[0]], pct);
                // Show queued units
                if (b.trainQueue[1] >= 0) {
                    iprintf("\x1b[20;0H Queue: %s", UNIT_NAMES[(u8)b.trainQueue[1]]);
                    if (b.trainQueue[2] >= 0)
                        iprintf(", %s", UNIT_NAMES[(u8)b.trainQueue[2]]);
                }
            } else {
                iprintf("\x1b[19;0H [Idle]");
            }
        }

        // Show available actions
        int line = 21;

        // TC: show age-up option
        if (b.type == BLDG_TOWN_CENTER && gs.players[0].ageProgress < 0 &&
            gs.players[0].age < AGE_IMPERIAL) {
            int na = gs.players[0].age + 1;
            iprintf("\x1b[%d;0H START:%s F:%d G:%d", line,
                    AGE_NAMES[na], AGE_COST[na][RES_FOOD], AGE_COST[na][RES_GOLD]);
            line++;
        }

        // Show tech research options
        for (int t = 0; t < TECH_COUNT && line < 23; t++) {
            if (TECH_TABLE[t].bldgReq == b.type &&
                !tech_is_researched(gs, 0, t) &&
                gs.players[0].age >= TECH_TABLE[t].ageReq) {
                iprintf("\x1b[%d;0H START:%s F:%d G:%d", line,
                        TECH_TABLE[t].name,
                        TECH_TABLE[t].cost[RES_FOOD], TECH_TABLE[t].cost[RES_GOLD]);
                line++;
            }
        }

        // Show trainable units
        for (int ut = 0; ut < UNIT_TYPE_COUNT && line < 24; ut++) {
            if (UNIT_STATS[ut].bldgReq == b.type && gs.players[0].age >= UNIT_STATS[ut].ageReq) {
                iprintf("\x1b[%d;0H  %s (F:%d W:%d G:%d)", line, UNIT_NAMES[ut],
                        UNIT_STATS[ut].cost[RES_FOOD], UNIT_STATS[ut].cost[RES_WOOD],
                        UNIT_STATS[ut].cost[RES_GOLD]);
                line++;
            }
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
        default: break;
        }
        iprintf("\x1b[17;0H %s", tileName);
        if (amt > 0) {
            iprintf("\x1b[18;0H %s: %d", resName, amt);
        }
    }

    // Game phase overlay
    if (gs.phase == PHASE_VICTORY) {
        iprintf("\x1b[10;8H  VICTORY!  ");
        iprintf("\x1b[11;6H Touch to restart");
    } else if (gs.phase == PHASE_DEFEAT) {
        iprintf("\x1b[10;8H  DEFEAT!   ");
        iprintf("\x1b[11;6H Touch to restart");
    }

    // Build menu hint
    if (gs.buildMenuOpen) {
        iprintf("\x1b[23;0H [Build Menu Open]");
    } else if (gs.inputMode == 1) {
        iprintf("\x1b[23;0H Place: %s", BLDG_NAMES[gs.placeBldgType]);
    } else if (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
               buildings[gs.selectedBldg].type == BLDG_TOWN_CENTER &&
               buildings[gs.selectedBldg].garrisonCount > 0) {
        iprintf("\x1b[23;0H A:Ungarrison START:Train");
    } else {
        iprintf("\x1b[23;0H X:Build L/R:Vil B:Cancel");
    }

    // Score comparison (right side, next to minimap)
    if (gs.phase == PHASE_PLAYING) {
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
        iprintf("\x1b[5;17HYou:%d", scores[0]);
        iprintf("\x1b[6;17H AI:%d", scores[1]);
    }
}
