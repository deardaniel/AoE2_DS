#include "ui.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "fog.h"
#include "tech.h"
#include <stdio.h>

// Access tech-modified stats
extern UnitStats playerUnitStats[NUM_PLAYERS][UNIT_TYPE_COUNT];

// Top screen: VRAM_A used as bitmap for minimap
// Console used for text overlay

static PrintConsole topConsole;
static u16* minimapVram = NULL;
static int minimapBg = -1;

// Minimap dimensions: 4px per tile = 128x128
enum { MINIMAP_SCALE = 4, MINIMAP_SIZE = MAP_TILES * MINIMAP_SCALE }; // 128x128
enum { MINIMAP_X = 0, MINIMAP_Y = 0 };

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

    // BG2: 256-color bitmap for minimap
    minimapBg = bgInit(2, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    minimapVram = (u16*)bgGetGfxPtr(minimapBg);
    bgSetPriority(minimapBg, 1);

    // BG0: console text (higher priority = on top)
    consoleInit(&topConsole, 0, BgType_Text4bpp, BgSize_T_256x256, 4, 0, true, true);
    bgSetPriority(topConsole.bgId, 0);

    consoleSelect(&topConsole);
}

static void ui_draw_minimap(const GameState& gs, const TerrainMap& terrain) {
    if (!minimapVram) return;

    // Draw terrain
    for (int ty = 0; ty < MAP_TILES; ty++) {
        for (int tx = 0; tx < MAP_TILES; tx++) {
            u8 ttype = terrain.tileAt(tx, ty);
            u8 fogState = fogMap.state[0][ty][tx];

            u16 color;
            if (fogState == FOG_UNEXPLORED) {
                color = RGB15(0, 0, 0);
            } else {
                color = terrainMiniColors[ttype];
                if (fogState == FOG_EXPLORED) {
                    // Darken for explored-but-not-visible
                    int r = (color & 0x1F) >> 1;
                    int g = ((color >> 5) & 0x1F) >> 1;
                    int b = ((color >> 10) & 0x1F) >> 1;
                    color = RGB15(r, g, b);
                }
            }
            // Set bit 15 for DS 16-bit bitmap display
            color |= BIT(15);

            // Fill minimap pixels
            for (int dy = 0; dy < MINIMAP_SCALE; dy++) {
                for (int dx = 0; dx < MINIMAP_SCALE; dx++) {
                    int mx = MINIMAP_X + tx * MINIMAP_SCALE + dx;
                    int my = MINIMAP_Y + ty * MINIMAP_SCALE + dy;
                    if (mx < 256 && my < 192) {
                        minimapVram[my * 256 + mx] = color;
                    }
                }
            }
        }
    }

    // Draw units on minimap
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
        int tx = (units[i].x + TILE_PX/2) / TILE_PX;
        int ty = (units[i].y + TILE_PX/2) / TILE_PX;

        // Only show if visible to player 0
        if (units[i].owner != 0 && !fogMap.isVisible(0, tx, ty)) continue;

        u16 dotColor = (units[i].owner == 0) ? RGB15(4, 8, 31) : RGB15(31, 4, 4);
        dotColor |= BIT(15);

        int mx = MINIMAP_X + tx * MINIMAP_SCALE + 1;
        int my = MINIMAP_Y + ty * MINIMAP_SCALE + 1;
        if (mx >= 0 && mx < 255 && my >= 0 && my < 191) {
            minimapVram[my * 256 + mx] = dotColor;
            minimapVram[my * 256 + mx + 1] = dotColor;
            minimapVram[(my+1) * 256 + mx] = dotColor;
            minimapVram[(my+1) * 256 + mx + 1] = dotColor;
        }
    }

    // Draw buildings on minimap (larger dots)
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (!buildings[i].alive) continue;
        int tx = buildings[i].x / TILE_PX;
        int ty = buildings[i].y / TILE_PX;

        if (buildings[i].owner != 0 && !fogMap.isExplored(0, tx, ty)) continue;

        u16 dotColor = (buildings[i].owner == 0) ? RGB15(4, 8, 31) : RGB15(31, 4, 4);
        dotColor |= BIT(15);
        int bw = BLDG_STATS[buildings[i].type].tileW * MINIMAP_SCALE;
        int bh = BLDG_STATS[buildings[i].type].tileH * MINIMAP_SCALE;

        for (int dy = 0; dy < bh; dy++) {
            for (int dx = 0; dx < bw; dx++) {
                int mx = MINIMAP_X + tx * MINIMAP_SCALE + dx;
                int my = MINIMAP_Y + ty * MINIMAP_SCALE + dy;
                if (mx >= 0 && mx < 256 && my >= 0 && my < 192) {
                    minimapVram[my * 256 + mx] = dotColor;
                }
            }
        }
    }

    // Draw camera viewport rectangle
    int vx1 = MINIMAP_X + (gs.camX * MINIMAP_SCALE) / TILE_PX;
    int vy1 = MINIMAP_Y + (gs.camY * MINIMAP_SCALE) / TILE_PX;
    int vx2 = vx1 + (SCREEN_W * MINIMAP_SCALE) / TILE_PX;
    int vy2 = vy1 + (SCREEN_H * MINIMAP_SCALE) / TILE_PX;
    u16 white = RGB15(31, 31, 31) | BIT(15);

    for (int x = vx1; x <= vx2; x++) {
        if (x >= 0 && x < 256) {
            if (vy1 >= 0 && vy1 < 192) minimapVram[vy1 * 256 + x] = white;
            if (vy2 >= 0 && vy2 < 192) minimapVram[vy2 * 256 + x] = white;
        }
    }
    for (int y = vy1; y <= vy2; y++) {
        if (y >= 0 && y < 192) {
            if (vx1 >= 0 && vx1 < 256) minimapVram[y * 256 + vx1] = white;
            if (vx2 >= 0 && vx2 < 256) minimapVram[y * 256 + vx2] = white;
        }
    }
}

void ui_update(const GameState& gs, const TerrainMap& terrain) {
    consoleSelect(&topConsole);

    // Draw minimap (only every 4th frame for performance)
    if ((gs.frameCount & 3) == 0) {
        ui_draw_minimap(gs, terrain);
    }

    // Clear console area (below minimap)
    // Console is overlaid on the bitmap, so we just print text
    consoleClear();

    const Player& p0 = gs.players[0];

    // Resource bar (line 0-1, positioned right of minimap)
    iprintf("\x1b[0;17H F:%d", p0.resources[RES_FOOD]);
    iprintf("\x1b[1;17H W:%d", p0.resources[RES_WOOD]);
    iprintf("\x1b[2;17H G:%d", p0.resources[RES_GOLD]);
    iprintf("\x1b[3;17H S:%d", p0.resources[RES_STONE]);

    // Age and pop
    iprintf("\x1b[5;17H %s", AGE_NAMES[p0.age]);
    iprintf("\x1b[6;17H Pop:%d/%d", p0.popCount, p0.popCap);

    // Age progress
    if (p0.ageProgress >= 0) {
        int nextAge = p0.age + 1;
        if (nextAge < AGE_COUNT) {
            int pct = (p0.ageProgress * 100) / AGE_RESEARCH_TIME[nextAge];
            iprintf("\x1b[7;17H Age Up:%d%%", pct);
        }
    }

    // Selected unit info (below minimap area)
    if (gs.selectedUnit >= 0 && gs.selectedUnit < MAX_UNITS && units[gs.selectedUnit].alive) {
        const Unit& u = units[gs.selectedUnit];
        const UnitStats& st = playerUnitStats[u.owner][u.type];
        iprintf("\x1b[17;0H %s", UNIT_NAMES[u.type]);
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
        iprintf("\x1b[18;0H HP:%d/%d", b.hp, st.hp);

        if (b.buildProgress < st.buildTime) {
            int pct = (b.buildProgress * 100) / st.buildTime;
            iprintf("\x1b[19;0H Building:%d%%", pct);
        } else {
            if (b.trainQueue[0] >= 0) {
                int pct = (b.trainProgress * 100) / UNIT_STATS[(u8)b.trainQueue[0]].trainTime;
                iprintf("\x1b[19;0H Train %s:%d%%",
                        UNIT_NAMES[(u8)b.trainQueue[0]], pct);
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
    } else {
        iprintf("\x1b[23;0H X:Build L/R:Vil B:Cancel");
    }
}
