// AoE2 DSi — Age of Empires 2 for Nintendo DSi
// Main entry point and game loop
#include <nds.h>
#include <stdio.h>

#include "config.h"
#include "game.h"
#include "terrain.h"
#include "iso.h"
#include "units.h"
#include "buildings.h"
#include "input.h"
#include "render.h"
#include "ui.h"
#include "fog.h"
#include "tech.h"
#include "ai.h"
#include "sound.h"
#include "font.h"
#include "projectiles.h"
#include "save.h"
#include "pause.h"
#include <filesystem.h>
#include <fat.h>

#ifdef SHOWCASE
// Profiling for debug builds: how much of a 1/60 s frame each part of the
// loop takes, in percent, shown under the minimap (ui.cpp).
// What is shown is the worst frame of the last second for each part (a
// spike is what gets felt, and a screenshot can't catch a single frame),
// and in profTotal the worst and the average whole frame.
int profPct[10];  // units, ai, ground, sprites, ui, buildings+projectiles, fog, path, overlays+copy
int profTotal[2];   // worst frame, average frame
int dbgFulls, dbgRects;
int dbgG[5], dbgGW[5];   // worst per second: shift+strips, static scroll, fog ground, fog static, full
#define G_T0() gT = cpuGetTiming()
#define G_LAP(n) do { u32 nowT = cpuGetTiming(); int pc = (int)((u64)(nowT - gT) * 100 / 560190); \
    if (pc > dbgGW[n]) dbgGW[n] = pc; gT = nowT; } while (0)
static int profNow[10], profWorst[10], profFrameSum, profFrameWorst, profFrames, profSumAll;
static u32 profMark;
#define PROF_BEGIN() do { cpuStartTiming(2); profMark = 0; profFrameSum = 0; } while (0)
#define PROF_LAP(slot) do { u32 now = cpuGetTiming(); \
    profNow[slot] = (int)((u64)(now - profMark) * 100 / 560190); profMark = now; \
    profFrameSum += profNow[slot]; \
    if (profNow[slot] > profWorst[slot]) profWorst[slot] = profNow[slot]; } while (0)
#define PROF_END() do { cpuEndTiming(); \
    if (profFrameSum > profFrameWorst) profFrameWorst = profFrameSum; \
    profSumAll += profFrameSum; \
    if (++profFrames == 60) { \
        for (int s = 0; s < 9; s++) { if (s == 7) continue; profPct[s] = profWorst[s]; profWorst[s] = 0; } \
        profTotal[0] = profFrameWorst; profTotal[1] = profSumAll / 60; \
        profFrames = 0; profFrameWorst = 0; profSumAll = 0; } } while (0)
#else
#define PROF_BEGIN()
#define PROF_LAP(slot)
#define PROF_END()
#endif

// Global game state (accessible by tech.cpp via extern)
GameState gameState;

static TerrainMap terrain;

// Main RAM framebuffer for bottom screen (NDS VRAM doesn't support byte writes)
static u8 terrainBuf[256 * 192] __attribute__((aligned(4)));
// The ground layer alone, redrawn only when it changes (see the main loop)
static u8 groundBuf[256 * 192] __attribute__((aligned(4)));
static u32 groundVersion = 0;

// ---------------------------------------------------------------------------
// Start a new game
// ---------------------------------------------------------------------------
static void game_start() {
    // Free all allocated OAM gfx before reinitializing (prevents VRAM leak on restart)
    for (int i = 0; i < MAX_UNITS; i++) {
        if (units[i].spriteGfx) {
            oamFreeGfx(&oamSub, units[i].spriteGfx);
            units[i].spriteGfx = NULL;
        }
    }
    for (int i = 0; i < MAX_BUILDINGS; i++) {
        if (buildings[i].spriteGfx) {
            oamFreeGfx(&oamSub, buildings[i].spriteGfx);
            buildings[i].spriteGfx = NULL;
        }
    }

    u8 savedDiff = gameState.aiDifficulty;
    game_init(gameState);
    gameState.aiDifficulty = savedDiff;
    units_init();
    buildings_init();
    projectiles_init();
    fogMap.init();
    tech_init_stats();
    ai_init();

    // Generate map with a random seed from hardware timer
    u32 seed = (u32)(TIMER0_DATA | (TIMER1_DATA << 16)) ^ (u32)gameState.frameCount;
    if (seed == 0) seed = 12345;
    terrain.generate(seed);

    // Temporarily give enough resources to place starting TCs (free in AoE2)
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gameState.players[p].resources[RES_WOOD]  = 9999;
        gameState.players[p].resources[RES_STONE] = 9999;
    }

    // Clear terrain under starting TC positions so building_place succeeds
    // (resource tiles like sheep/berries block canBuild)
    for (int dy = 0; dy < 4; dy++) {
        for (int dx = 0; dx < 4; dx++) {
            terrain.setTile(2 + dx, 2 + dy, TERRAIN_GRASS, 0);
            terrain.setTile(MAP_TILES - 6 + dx, MAP_TILES - 6 + dy, TERRAIN_GRASS, 0);
        }
    }

    // --- Player 0 (human) — top-left corner ---
    // Place Town Center at tile (2,2) — 4x4 building
    int tc0 = building_place(BLDG_TOWN_CENTER, 0, 2, 2, gameState, terrain);
    if (tc0 >= 0) {
        building_complete_now(tc0);
    }

    // Spawn 3 villagers near TC (below and right of 4x4 TC)
    unit_spawn(UNIT_VILLAGER, 0, 6 * TILE_PX, 6 * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 0, 7 * TILE_PX, 6 * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 0, 6 * TILE_PX, 7 * TILE_PX);

    // Spawn scout cavalry near TC (auto-scouts by default)
    {
        int si = unit_spawn(UNIT_SCOUT, 0, 7 * TILE_PX, 7 * TILE_PX);
        if (si >= 0) units[si].state = USTATE_SCOUTING;
    }

    // Spawn sheep near player 0 TC (herdable food source)
    unit_spawn(UNIT_SHEEP, 0, 0 * TILE_PX, 3 * TILE_PX);
    unit_spawn(UNIT_SHEEP, 0, 1 * TILE_PX, 3 * TILE_PX);
    unit_spawn(UNIT_SHEEP, 0, 0 * TILE_PX, 4 * TILE_PX);
    unit_spawn(UNIT_SHEEP, 0, 1 * TILE_PX, 4 * TILE_PX);

#ifdef SHOWCASE
    // Debug builds for checking sprites in one screenshot (make SHOWCASE=n):
    //   1 = the smaller buildings beside the player's TC
    //   2 = one of every unit, turned to a new facing every 2 seconds
    //   3 = normal start, one villager scripted onto a sheep
    //   4 = the 4x4 buildings, each construction stage and an enemy building
    //   5 = normal game watched from the AI's base with the fog lifted
#if SHOWCASE != 3 && SHOWCASE != 5
    for (int ty = 0; ty < 26; ty++)
        for (int tx = 0; tx < 26; tx++)
            if (tx < 2 || tx > 5 || ty < 2 || ty > 5)
                terrain.setTile(tx, ty, TERRAIN_GRASS, 0);
#endif
#if SHOWCASE == 1 || SHOWCASE == 4
    {
        // progress: construction stage 0-2, or 3 = finished
        static const struct { u8 type, owner, tx, ty, progress; } LAYOUT[] = {
            { BLDG_HOUSE, 0, 8, 3, 3 },        { BLDG_TOWER, 0, 11, 3, 3 },
            { BLDG_WALL, 0, 13, 6, 3 },        { BLDG_WALL, 0, 14, 6, 3 },
            { BLDG_MINING_CAMP, 0, 2, 7, 3 },  { BLDG_LUMBER_CAMP, 0, 1, 10, 3 },
            { BLDG_BARRACKS, 0, 7, 6, 3 },     { BLDG_ARCHERY_RANGE, 0, 11, 8, 3 },
            { BLDG_STABLE, 0, 4, 10, 3 },      { BLDG_MONASTERY, 0, 8, 11, 3 },
            { BLDG_MARKET, 0, 9, 16, 3 },      { BLDG_CASTLE, 0, 14, 12, 3 },
            { BLDG_UNIVERSITY, 0, 14, 17, 3 }, { BLDG_HOUSE, 1, 19, 16, 3 },
            { BLDG_HOUSE, 0, 12, 21, 0 },      { BLDG_BARRACKS, 0, 8, 21, 1 },
            { BLDG_MARKET, 0, 19, 19, 2 },     { BLDG_TOWER, 0, 15, 22, 0 },
        };
        for (unsigned i = 0; i < sizeof(LAYOUT) / sizeof(LAYOUT[0]); i++) {
            Player& pl = gameState.players[LAYOUT[i].owner];
            u8 savedAge = pl.age;
            pl.age = AGE_IMPERIAL;
            pl.resources[RES_WOOD] = 9999;
            pl.resources[RES_STONE] = 9999;
            int bi = building_place(LAYOUT[i].type, LAYOUT[i].owner, LAYOUT[i].tx, LAYOUT[i].ty,
                                    gameState, terrain);
            if (bi >= 0) {
                if (LAYOUT[i].progress >= 3) building_complete_now(bi);
                else buildings[bi].buildProgress =
                    BLDG_STATS[LAYOUT[i].type].buildTime * LAYOUT[i].progress / 3 + 1;
            }
            pl.age = savedAge;
        }
    }
#elif SHOWCASE == 2
    // One of each unit type in two rows; main loop turns them every 2 seconds
    for (int t = 0; t < UNIT_TYPE_COUNT; t++) {
        int k = t % 5, row = t / 5;
        unit_spawn(t, 0, (5 + row * 2 + k) * TILE_PX, (9 + row * 2 - k) * TILE_PX);
    }
#endif
#endif

    // --- Player 1 (AI) — bottom-right corner ---
    int tc1 = building_place(BLDG_TOWN_CENTER, 1, MAP_TILES - 6, MAP_TILES - 6, gameState, terrain);
    if (tc1 >= 0) {
        building_complete_now(tc1);
    }

    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 2) * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 1) * TILE_PX, (MAP_TILES - 2) * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 1) * TILE_PX);

    // Spawn AI scout near TC (auto-scouts by default)
    {
        int si = unit_spawn(UNIT_SCOUT, 1, (MAP_TILES - 1) * TILE_PX, (MAP_TILES - 1) * TILE_PX);
        if (si >= 0) units[si].state = USTATE_SCOUTING;
    }

    // Spawn sheep near AI TC
    unit_spawn(UNIT_SHEEP, 1, (MAP_TILES - 1) * TILE_PX, (MAP_TILES - 7) * TILE_PX);
    unit_spawn(UNIT_SHEEP, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 7) * TILE_PX);
    unit_spawn(UNIT_SHEEP, 1, (MAP_TILES - 1) * TILE_PX, (MAP_TILES - 8) * TILE_PX);
    unit_spawn(UNIT_SHEEP, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 8) * TILE_PX);

    // Reset resources to actual starting values (TC placement was free)
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gameState.players[p].resources[RES_FOOD]  = 200;
        gameState.players[p].resources[RES_WOOD]  = 200;
        gameState.players[p].resources[RES_GOLD]  = 100;
        gameState.players[p].resources[RES_STONE] = 200;
    }

    // Hard AI gets extra starting resources
    if (gameState.aiDifficulty == AI_HARD) {
        for (int r = 0; r < RES_COUNT; r++)
            gameState.players[1].resources[r] += 100;
    }

    // Update initial fog and pop caps
    fogMap.update();
    game_update_pop_cap(gameState, 0);
    game_update_pop_cap(gameState, 1);

    // Center camera on player's TC (3x3, center at tile 3.5, 3.5) in iso space
    int isoX, isoY;
    tileToIso(4, 4, isoX, isoY);
    gameState.camX = isoX - SCREEN_W / 2;
    gameState.camY = isoY - SCREEN_H / 2;
    if (gameState.camX < 0) gameState.camX = 0;
    if (gameState.camY < 0) gameState.camY = 0;
#if defined(SHOWCASE) && SHOWCASE == 4
    tileToIso(14, 17, isoX, isoY);
    gameState.camX = isoX - SCREEN_W / 2;
    gameState.camY = isoY - SCREEN_H / 2;
#elif defined(SHOWCASE) && SHOWCASE == 5
    tileToIso(MAP_TILES - 7, MAP_TILES - 7, isoX, isoY);
    gameState.camX = isoX - SCREEN_W / 2;
    gameState.camY = isoY - SCREEN_H / 2;
#elif defined(SHOWCASE) && SHOWCASE != 3
    gameState.camY = 24;
#endif
}

// ---------------------------------------------------------------------------
// Draw the ground (terrain, then the fog over it) into groundBuf, inside a
// rectangle of the screen
// ---------------------------------------------------------------------------
static void draw_ground(int cx0, int cy0, int cx1, int cy1) {
    if (cx0 >= cx1 || cy0 >= cy1) return;
    terrain.renderViewport(groundBuf, gameState.camX, gameState.camY, cx0, cy0, cx1, cy1);

    // Tile range under the rectangle, from its corners
    int minTX, minTY, maxTX, maxTY, tmpTX, tmpTY;
    screenToTile(cx0, cy0, gameState.camX, gameState.camY, minTX, minTY);
    maxTX = minTX; maxTY = minTY;
    const int corner[3][2] = { { cx1, cy0 }, { cx0, cy1 }, { cx1, cy1 } };
    for (int c = 0; c < 3; c++) {
        screenToTile(corner[c][0], corner[c][1], gameState.camX, gameState.camY, tmpTX, tmpTY);
        if (tmpTX < minTX) minTX = tmpTX;
        if (tmpTX > maxTX) maxTX = tmpTX;
        if (tmpTY < minTY) minTY = tmpTY;
        if (tmpTY > maxTY) maxTY = tmpTY;
    }
    minTX -= 1; minTY -= 1;
    maxTX += 1; maxTY += 1;

    for (int tx = minTX; tx <= maxTX; tx++) {
        for (int ty = minTY; ty <= maxTY; ty++) {
            if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;
            u8 fogState = fogMap.state[0][ty][tx];
            if (fogState == FOG_VISIBLE) continue;

            int isoFX, isoFY;
            tileToIso(tx, ty, isoFX, isoFY);
            int dstX = isoFX - gameState.camX;
            int dstY = isoFY - gameState.camY;
            if (dstX + ISO_TILE_W + 1 <= cx0 || dstX - 1 >= cx1) continue;
            if (dstY + ISO_TILE_H <= cy0 || dstY >= cy1) continue;

            for (int py = 0; py < ISO_TILE_H; py++) {
                int screenY = dstY + py;
                if (screenY < cy0 || screenY >= cy1) continue;
                int xs = ISO_DIAMOND_XSTART[py];
                int xe = ISO_DIAMOND_XEND[py];
                // Match the extended diamond used in renderViewport
                if (!terrain.showTileGrid) {
                    if (xs > 0) xs--;
                    if (xe < ISO_TILE_W) xe++;
                }
                for (int px = xs; px < xe; px++) {
                    int screenX = dstX + px;
                    if (screenX < cx0 || screenX >= cx1) continue;
                    // Unexplored: black. Explored but out of sight: a
                    // checkerboard of black, fixed to the map
                    if (fogState == FOG_UNEXPLORED || ((px + py) & 1))
                        groundBuf[screenY * 256 + screenX] = PAL_BLACK;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(void) {
    // --- Sub screen (bottom): bitmap BG + OAM sprites for game ---
    videoSetModeSub(MODE_5_2D);
    vramSetBankC(VRAM_C_SUB_BG);
    vramSetBankD(VRAM_D_SUB_SPRITE);

    oamInit(&oamSub, SpriteMapping_1D_128, false);
    oamEnable(&oamSub);

    // Sub BG2: 8-bit bitmap for terrain
    int bg2 = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    u8* subVram = (u8*)bgGetGfxPtr(bg2);

    // --- Top screen (main): handled by ui_init ---
    // VRAM_B mapped to main BG at 0x06020000 for minimap bitmap
    // (VRAM_A at 0x06000000 is used for console tiles/map)
    vramSetBankB(VRAM_B_MAIN_BG);

    ui_init();  // sets up main engine video mode, VRAM_A, console

    // Load shared terrain palette into BG_PALETTE_SUB (includes UI colors at 0-15)
    terrain_initPalette();

    // Init terrain tile graphics cache from preprocessed binary data
    terrain.initTileGfx();

    // Init render system
    render_init();

    // Init bitmap font
    font_init();

    // Init FAT filesystem (for save/load to SD card)
    fatInitDefault();

    // Init NitroFS (for streaming music from ROM filesystem)
    nitroFSInit(NULL);

    // Init sound
    sound_init();

    // Start background music
    sound_music_start();

    // Start free-running hardware timers for random seed generation
    TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1;
    TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE;

    // Start first game (default: Normal difficulty)
    gameState.aiDifficulty = AI_NORMAL;
    game_start();

    // === Main Loop ===
    while (pmMainLoop()) {
        // Pause menu: the game stands still under it. The last frame drawn
        // is still in terrainBuf; the menu dims it and draws on top.
        if (pause_is_open()) {
            scanKeys();
            PauseResult r = pause_frame(terrainBuf, gameState, terrain);
            if (r == PAUSE_NEW_GAME) game_start();
            if (r == PAUSE_STAY) {
                DC_FlushRange(terrainBuf, 256 * 192);
                dmaCopy(terrainBuf, subVram, 256 * 192);
            }
            sound_music_update();
            swiWaitForVBlank();
            continue;
        }

        // Handle game-over touch-to-restart
        if (gameState.phase != PHASE_PLAYING) {
            scanKeys();
            int kd = keysDown();
            if (kd & KEY_TOUCH) {
                game_start();
            }
            // START: the saved games
            if ((kd & KEY_START) && save_available()) {
                pause_open(true);
            }
            // L/R cycle AI difficulty on game-over screen
            if (kd & KEY_R) {
                gameState.aiDifficulty = (gameState.aiDifficulty + 1) % 3;
            }
            if (kd & KEY_L) {
                gameState.aiDifficulty = (gameState.aiDifficulty + 2) % 3;
            }

            // Render game world frozen underneath
            terrain.renderViewport(groundBuf, gameState.camX, gameState.camY);
            render_sprites_sw(terrainBuf, gameState, terrain, groundBuf, ++groundVersion);

            // Darken the entire bottom screen with checkerboard
            for (int py = 0; py < SCREEN_H; py++) {
                for (int px = 0; px < SCREEN_W; px++) {
                    if ((px + py) & 1)
                        terrainBuf[py * 256 + px] = PAL_BLACK;
                }
            }

            // Draw centered result text on bottom screen
            {
                bool victory = (gameState.phase == PHASE_VICTORY);
                const char* title = victory ? "VICTORY!" : "DEFEAT!";
                u8 titleCol = victory ? PAL_YELLOW : PAL_RED;

                // Center the title text
                int tw = font_string_width(gameFont, title);
                int tx = (SCREEN_W - tw) / 2;
                font_draw_str_8(terrainBuf, 256, 192, tx, 60, title, titleCol, gameFont);

                // Stats
                int sy = 84;
                u8 sc = PAL_WHITE;
                int lx = 60;

                font_draw_str_8(terrainBuf, 256, 192, lx, sy, "Units killed:", sc, gameFont);
                font_draw_num_8(terrainBuf, 256, 192, lx + 95, sy, gameState.unitsKilled[0], sc, gameFont);
                sy += 14;

                font_draw_str_8(terrainBuf, 256, 192, lx, sy, "Units lost:", sc, gameFont);
                font_draw_num_8(terrainBuf, 256, 192, lx + 95, sy, gameState.unitsLost[0], sc, gameFont);
                sy += 14;

                font_draw_str_8(terrainBuf, 256, 192, lx, sy, "Bldgs razed:", sc, gameFont);
                font_draw_num_8(terrainBuf, 256, 192, lx + 95, sy, gameState.bldgsDestroyed[0], sc, gameFont);
                sy += 20;

                // Difficulty selector
                static const char* DIFF_NAMES[] = {"Easy", "Normal", "Hard"};
                u8 d = gameState.aiDifficulty;
                if (d > 2) d = 1;
                int dx = font_draw_str_8(terrainBuf, 256, 192, lx, sy, "AI: ", sc, gameFont);
                font_draw_str_8(terrainBuf, 256, 192, dx, sy, DIFF_NAMES[d], PAL_YELLOW, gameFont);
                sy += 14;
                font_draw_str_8(terrainBuf, 256, 192, lx, sy, "L/R:Change", sc, gameFont);
                sy += 14;
                if (save_available())
                    font_draw_str_8(terrainBuf, 256, 192, lx, sy, "START:Load game", sc, gameFont);
                sy += 20;

                const char* hint = "Touch to restart";
                int hw = font_string_width(gameFont, hint);
                int hx = (SCREEN_W - hw) / 2;
                // Flash the hint text
                if ((gameState.frameCount >> 4) & 1)
                    font_draw_str_8(terrainBuf, 256, 192, hx, sy, hint, PAL_WHITE, gameFont);
            }

            DC_FlushRange(terrainBuf, 256 * 192);
            dmaCopy(terrainBuf, subVram, 256 * 192);

            // Still update UI to show victory/defeat on top screen
            ui_update(gameState, terrain);
            gameState.frameCount++;
            swiWaitForVBlank();
            continue;
        }

        // Input
        input_update(gameState, terrain);

        // START: pause menu (resume, save, load, music, new game)
        if (keysDown() & KEY_START) {
            pause_open();
            continue;
        }

        // D-pad up/down: move the highlight along a building's unit list
        {
            int kp = keysDown();
            if ((kp & KEY_UP) || (kp & KEY_DOWN)) {
                if (gameState.selectedBldg >= 0 && buildings[gameState.selectedBldg].alive &&
                    building_is_complete(gameState.selectedBldg) &&
                    buildings[gameState.selectedBldg].owner == 0) {
                    Building& b = buildings[gameState.selectedBldg];
                    int trainable[UNIT_TYPE_COUNT];
                    int trainCount = 0;
                    for (int ut = 0; ut < UNIT_TYPE_COUNT; ut++) {
                        if (UNIT_STATS[ut].bldgReq == b.type &&
                            gameState.players[0].age >= UNIT_STATS[ut].ageReq) {
                            trainable[trainCount++] = ut;
                        }
                    }
                    if (trainCount > 1) {
                        int curIdx = 0;
                        for (int t = 0; t < trainCount; t++) {
                            if (trainable[t] == gameState.trainUnitType) { curIdx = t; break; }
                        }
                        if (kp & KEY_DOWN) curIdx = (curIdx + 1) % trainCount;
                        if (kp & KEY_UP)   curIdx = (curIdx + trainCount - 1) % trainCount;
                        gameState.trainUnitType = trainable[curIdx];
                    }
                }
            }
        }

        // Game logic
        PROF_BEGIN();
#ifdef SHOWCASE
        {
            extern int profPathSearches; extern u32 profPathTicks;
            profPathSearches = 0; profPathTicks = 0;
        }
#endif
        units_update(gameState, terrain);
        PROF_LAP(0);
#ifdef SHOWCASE
        {
            // Hold the worst frame of the last second so a screenshot catches it
            extern u32 profPathTicks;
            static int worstPct;
            int pct = (int)((u64)profPathTicks * 100 / 560190);
            if (pct > worstPct) worstPct = pct;
            if (gameState.frameCount % 60 == 0) { profPct[7] = worstPct; worstPct = 0; }
        }
#endif
        buildings_update(gameState, terrain);
        projectiles_update(gameState);
        game_update(gameState);
        PROF_LAP(5);

        // Fog of war (every fourth frame, between the top screen redraws)
        if ((gameState.frameCount & 3) == 1) {
            fogMap.update();
        }
        PROF_LAP(6);

#if defined(SHOWCASE) && SHOWCASE == 5
        memset(fogMap.state[0], FOG_VISIBLE, sizeof(fogMap.state[0]));
#endif

        // AI
        ai_update(gameState, terrain);
#if defined(SHOWCASE) && SHOWCASE == 3
        // Scripted check: every villager goes for a sheep 2 seconds in
        if (gameState.frameCount == 120) {
            int sheep = -1;
            for (int i = 0; i < MAX_UNITS; i++)
                if (units[i].alive && units[i].owner == 0 && units[i].type == UNIT_SHEEP) { sheep = i; break; }
            for (int i = 0; i < MAX_UNITS && sheep >= 0; i++)
                if (units[i].alive && units[i].owner == 0 && units[i].type == UNIT_VILLAGER) {
                    unit_command_attack(i, sheep);
                    break;
                }
        }
#endif
#if defined(SHOWCASE) && SHOWCASE == 2
        if (gameState.frameCount % 120 == 0)
            for (int i = 0; i < MAX_UNITS; i++)
                if (units[i].alive && units[i].state == USTATE_IDLE)
                    units[i].direction = (units[i].direction + 1) % DIR_COUNT;
#endif

        // Under attack alert timer
        if (gameState.underAttackTimer > 0) gameState.underAttackTimer--;

        // --- Rendering ---

        // Sub screen: render terrain to main RAM buffer
        // (NDS VRAM doesn't support byte writes — STRB is silently dropped)
        PROF_LAP(1);
        // The ground (terrain plus fog) is kept in groundBuf. When only the
        // camera moved, the picture is shifted and just the strips that came
        // into view are drawn — scrolling used to redraw the whole ground and
        // everything standing on it, more than a frame's work, every frame.
        // A change to the terrain or to the fog in view redraws it all.
        {
            static int gCamX = 0, gCamY = 0;
            static u32 gTerrain = 0;
            static bool gGrid = false, gHave = false;

            int x0, y0, x1, y1, tx, ty;
            screenToTile(0, 0, gameState.camX, gameState.camY, x0, y0);
            x1 = x0; y1 = y0;
            static const int CORNER[3][2] = { { SCREEN_W, 0 }, { 0, SCREEN_H }, { SCREEN_W, SCREEN_H } };
            for (int c = 0; c < 3; c++) {
                screenToTile(CORNER[c][0], CORNER[c][1], gameState.camX, gameState.camY, tx, ty);
                if (tx < x0) x0 = tx;
                if (tx > x1) x1 = tx;
                if (ty < y0) y0 = ty;
                if (ty > y1) y1 = ty;
            }
            bool full = !gHave || gTerrain != terrain.version || gGrid != terrain.showTileGrid;
#ifdef SHOWCASE
            if (keysHeld() & KEY_X) full = true;   // debug: compare a scrolled picture with a fresh one
#endif
            int dx = gameState.camX - gCamX, dy = gameState.camY - gCamY;
            if (!full && (dx != 0 || dy != 0)) {
                if (dx <= -96 || dx >= 96 || dy <= -64 || dy >= 64) {
                    full = true;
                } else {
#ifdef SHOWCASE
                    u32 G_T0();
#endif
                    render_shift(groundBuf, dx, dy);
                    ClipRect strips[2];
                    int n = scroll_strips(dx, dy, strips);
                    for (int k = 0; k < n; k++)
                        draw_ground(strips[k].x0, strips[k].y0, strips[k].x1, strips[k].y1);
#ifdef SHOWCASE
                    G_LAP(0);
#endif
                    render_static_scroll(groundBuf, gameState, terrain, dx, dy);
#ifdef SHOWCASE
                    G_LAP(1);
#endif
                }
            }

            // Fog: the tiles in view whose shading changed are redrawn one by
            // one (a unit walking moves a ring of them every few frames) —
            // a few per frame, the rest left flagged for the next, so the
            // work is spread out instead of landing on one frame. With a
            // long backlog the whole picture is cheaper.
            // (Trees reach up from a few rows below the screen, hence +5.)
            enum { FOG_RECTS_PER_FRAME = 4, FOG_BACKLOG_MAX = 96, MAX_FOG_RECTS = FOG_RECTS_PER_FRAME + 1 };
            static ClipRect fogRects[MAX_FOG_RECTS];
            static s8 later[FOG_BACKLOG_MAX][2];   // shade changes put off
            int laterCount = 0;
            int fogCount = 0;
            // Newly explored tiles come in a band along the edge of sight:
            // one rectangle round them all
            ClipRect newRect = { SCREEN_W, SCREEN_H, 0, 0 };
#if !(defined(SHOWCASE) && SHOWCASE == 5)   // (there the fog is forced visible every frame)
            for (int fy = y0 - 1; fy <= y1 + 5 && !full; fy++) {
                for (int fx = x0 - 1; fx <= x1 + 5 && !full; fx++) {
                    if (fx < 0 || fx >= MAP_TILES || fy < 0 || fy >= MAP_TILES) continue;
                    u8 kind = fogMap.changed[fy][fx];
                    if (!kind) continue;
                    int isoX, isoY;
                    tileToIso(fx, fy, isoX, isoY);
                    int sx = isoX - gameState.camX, sy = isoY - gameState.camY;
                    // Newly explored: the tree on it and the blending of its
                    // neighbours appear as well
                    ClipRect r = (kind == FOG_CHANGED_NEW)
                        ? ClipRect{ sx - ISO_TILE_W - 1, sy - 72, sx + 2 * ISO_TILE_W + 1, sy + 2 * ISO_TILE_H }
                        : ClipRect{ sx - 1, sy, sx + ISO_TILE_W + 1, sy + ISO_TILE_H };
                    if (r.x0 < 0) r.x0 = 0;
                    if (r.y0 < 0) r.y0 = 0;
                    if (r.x1 > SCREEN_W) r.x1 = SCREEN_W;
                    if (r.y1 > SCREEN_H) r.y1 = SCREEN_H;
                    if (r.x0 >= r.x1 || r.y0 >= r.y1) continue;
                    if (kind == FOG_CHANGED_NEW) {
                        if (r.x0 < newRect.x0) newRect.x0 = r.x0;
                        if (r.y0 < newRect.y0) newRect.y0 = r.y0;
                        if (r.x1 > newRect.x1) newRect.x1 = r.x1;
                        if (r.y1 > newRect.y1) newRect.y1 = r.y1;
                        continue;
                    }
                    if (fogCount == FOG_RECTS_PER_FRAME) {
                        if (laterCount == FOG_BACKLOG_MAX) { full = true; break; }
                        later[laterCount][0] = fx;
                        later[laterCount][1] = fy;
                        laterCount++;
                        continue;
                    }
                    fogRects[fogCount++] = r;
                }
            }
#endif
            // Flags outside the view are dropped (those tiles are drawn
            // afresh when they scroll in); the ones put off stay set
            memset(fogMap.changed, 0, sizeof(fogMap.changed));
            if (!full)
                for (int k = 0; k < laterCount; k++)
                    fogMap.changed[later[k][1]][later[k][0]] = FOG_CHANGED_SHADE;

            bool fogNew = newRect.x0 < newRect.x1;
            if (fogNew) {
                // More than half the screen: the whole picture is cheaper
                if ((newRect.x1 - newRect.x0) * (newRect.y1 - newRect.y0) > SCREEN_W * SCREEN_H / 2) full = true;
                else fogRects[fogCount++] = newRect;
            }
#ifdef SHOWCASE
            {
                // Debug: full redraws and most fog rectangles in a frame, per second
                extern int dbgFulls, dbgRects;
                static int fulls, rects, frames;
                if (full) fulls++;
                if (!full && fogCount > rects) rects = fogCount;
                if (++frames == 60) { dbgFulls = fulls; dbgRects = rects; fulls = rects = frames = 0; }
            }
#endif
#ifdef SHOWCASE
            u32 G_T0();
#endif
            if (full) {
                draw_ground(0, 0, SCREEN_W, SCREEN_H);
                groundVersion++;
#ifdef SHOWCASE
                G_LAP(4);
#endif
            } else if (fogCount > 0) {
                for (int k = 0; k < fogCount; k++)
                    draw_ground(fogRects[k].x0, fogRects[k].y0, fogRects[k].x1, fogRects[k].y1);
#ifdef SHOWCASE
                G_LAP(2);
#endif
                render_static_refresh(groundBuf, gameState, terrain, fogRects, fogCount, fogNew);
#ifdef SHOWCASE
                G_LAP(3);
#endif
            }
#ifdef SHOWCASE
            if (gameState.frameCount % 60 == 0)
                for (int q = 0; q < 5; q++) { dbgG[q] = dbgGW[q]; dbgGW[q] = 0; }
#endif
            gHave = true;
            gCamX = gameState.camX; gCamY = gameState.camY;
            gTerrain = terrain.version; gGrid = terrain.showTileGrid;
        }

        // Sub screen: software-render units and buildings into buffer
        PROF_LAP(2);
        render_sprites_sw(terrainBuf, gameState, terrain, groundBuf, groundVersion);
        PROF_LAP(3);

        // Sub screen: move target marker
        render_move_target(terrainBuf, gameState);

        // Sub screen: attack visualization lines
        render_projectiles(terrainBuf, gameState);

        // Sub screen: training progress bars
        render_training_bars(terrainBuf, gameState);

        // Sub screen: everything in production, top-left
        render_global_queue(terrainBuf, gameState);

        // Sub screen: building placement preview (diamond footprint)
        render_placement_preview(terrainBuf, gameState, terrain);

        // Sub screen: build menu overlay on buffer
        render_build_menu(terrainBuf, gameState);

        // Sub screen: train menu overlay (military buildings)
        render_train_menu(terrainBuf, gameState);

        // Sub screen: age advancement progress bar
        render_age_progress(terrainBuf, gameState);

        // Sub screen: drag-selection box overlay
        render_drag_box(terrainBuf, gameState);

        // DMA copy completed buffer to VRAM. DMA reads memory directly, so
        // what is still in the data cache has to be written out first (an
        // emulator shows the picture either way; the console would not).
        DC_FlushRange(terrainBuf, 256 * 192);
        dmaCopy(terrainBuf, subVram, 256 * 192);
        PROF_LAP(8);

        // Top screen: minimap + info panel
        ui_update(gameState, terrain);
        PROF_LAP(4);
        PROF_END();

        // Stream music from NitroFS (manual mode)
        sound_music_update();

        swiWaitForVBlank();
    }

    return 0;
}
