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
#include <filesystem.h>
#include <fat.h>

// Global game state (accessible by tech.cpp via extern)
GameState gameState;

static TerrainMap terrain;

// Main RAM framebuffer for bottom screen (NDS VRAM doesn't support byte writes)
static u8 terrainBuf[256 * 192] __attribute__((aligned(4)));

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
    // Debug build (make SHOWCASE=1 or 2): lay out every building, or every
    // unit, next to the player's TC so sprites can be checked in one screenshot.
#if SHOWCASE != 3
    for (int ty = 0; ty < 18; ty++)
        for (int tx = 0; tx < 18; tx++)
            if (tx < 2 || tx > 5 || ty < 2 || ty > 5)
                terrain.setTile(tx, ty, TERRAIN_GRASS, 0);
#endif
#if SHOWCASE == 1
    {
        static const struct { u8 type, owner, tx, ty; } LAYOUT[] = {
            { BLDG_HOUSE, 0, 7, 3 },      { BLDG_HOUSE, 1, 12, 7 },    { BLDG_TOWER, 0, 9, 3 },
            { BLDG_WALL, 0, 11, 5 },      { BLDG_WALL, 0, 12, 5 },     { BLDG_MINING_CAMP, 0, 2, 7 },
            { BLDG_LUMBER_CAMP, 0, 2, 9 },{ BLDG_BARRACKS, 0, 8, 6 },  { BLDG_ARCHERY_RANGE, 0, 10, 8 },
            { BLDG_STABLE, 0, 4, 8 },     { BLDG_MARKET, 0, 7, 10 },   { BLDG_MONASTERY, 0, 10, 11 },
            { BLDG_UNIVERSITY, 0, 13, 9 },{ BLDG_CASTLE, 0, 5, 12 },
        };
        for (unsigned i = 0; i < sizeof(LAYOUT) / sizeof(LAYOUT[0]); i++) {
            u8 savedAge = gameState.players[LAYOUT[i].owner].age;
            gameState.players[LAYOUT[i].owner].age = AGE_IMPERIAL;
            gameState.players[LAYOUT[i].owner].resources[RES_WOOD]  = 9999;
            gameState.players[LAYOUT[i].owner].resources[RES_STONE] = 9999;
            int bi = building_place(LAYOUT[i].type, LAYOUT[i].owner, LAYOUT[i].tx, LAYOUT[i].ty,
                                    gameState, terrain);
            building_complete_now(bi);
            gameState.players[LAYOUT[i].owner].age = savedAge;
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
#if defined(SHOWCASE) && SHOWCASE != 3
    gameState.camY = 24;
#endif
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
        // Handle game-over touch-to-restart
        if (gameState.phase != PHASE_PLAYING) {
            scanKeys();
            int kd = keysDown();
            if (kd & KEY_TOUCH) {
                game_start();
            }
            // START: load saved game
            if ((kd & KEY_START) && save_exists()) {
                load_game(terrain);
            }
            // L/R cycle AI difficulty on game-over screen
            if (kd & KEY_R) {
                gameState.aiDifficulty = (gameState.aiDifficulty + 1) % 3;
            }
            if (kd & KEY_L) {
                gameState.aiDifficulty = (gameState.aiDifficulty + 2) % 3;
            }

            // Render game world frozen underneath
            terrain.renderViewport(terrainBuf, gameState.camX, gameState.camY);
            render_sprites_sw(terrainBuf, gameState, terrain);

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
                if (save_exists())
                    font_draw_str_8(terrainBuf, 256, 192, lx, sy, "START:Load", sc, gameFont);
                sy += 20;

                const char* hint = "Touch to restart";
                int hw = font_string_width(gameFont, hint);
                int hx = (SCREEN_W - hw) / 2;
                // Flash the hint text
                if ((gameState.frameCount >> 4) & 1)
                    font_draw_str_8(terrainBuf, 256, 192, hx, sy, hint, PAL_WHITE, gameFont);
            }

            dmaCopy(terrainBuf, subVram, 256 * 192);

            // Still update UI to show victory/defeat on top screen
            ui_update(gameState, terrain);
            gameState.frameCount++;
            swiWaitForVBlank();
            continue;
        }

        // Input
        input_update(gameState, terrain);

        // Save game: L+R held, press SELECT
        {
            int kh = keysHeld();
            int kd2 = keysDown();
            if ((kh & (KEY_L | KEY_R)) == (KEY_L | KEY_R) && (kd2 & KEY_SELECT)) {
                save_game(terrain);
            }
        }

        // Handle training/research from selected building via START
        // Note: scanKeys() already called in input_update() — reuse keysDown()
        {
            int kp = keysDown();

            // D-pad up/down: cycle train unit type when military building selected
            if ((kp & KEY_UP) || (kp & KEY_DOWN)) {
                if (gameState.selectedBldg >= 0 && buildings[gameState.selectedBldg].alive &&
                    building_is_complete(gameState.selectedBldg) &&
                    buildings[gameState.selectedBldg].owner == 0) {
                    Building& b = buildings[gameState.selectedBldg];
                    // Only for buildings that train units (not market, not farm, etc.)
                    // Build list of trainable unit types for this building
                    int trainable[UNIT_TYPE_COUNT];
                    int trainCount = 0;
                    for (int ut = 0; ut < UNIT_TYPE_COUNT; ut++) {
                        if (UNIT_STATS[ut].bldgReq == b.type &&
                            gameState.players[0].age >= UNIT_STATS[ut].ageReq) {
                            trainable[trainCount++] = ut;
                        }
                    }
                    if (trainCount > 1) {
                        // Find current index
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

            if (kp & KEY_START) {
                if (gameState.selectedBldg >= 0 && buildings[gameState.selectedBldg].alive &&
                    building_is_complete(gameState.selectedBldg)) {
                    Building& b = buildings[gameState.selectedBldg];
                    bool didAction = false;

                    // TC: age advancement (priority over training)
                    if (b.type == BLDG_TOWN_CENTER && gameState.players[0].ageProgress < 0 &&
                        gameState.players[0].age < AGE_IMPERIAL) {
                        int nextAge = gameState.players[0].age + 1;
                        if (game_can_afford(gameState, 0, AGE_COST[nextAge])) {
                            game_deduct_cost(gameState, 0, AGE_COST[nextAge]);
                            gameState.players[0].ageProgress = 0;
                            didAction = true;
                        }
                    }

                    // Military buildings: research tech if available
                    if (!didAction) {
                        for (int t = 0; t < TECH_COUNT; t++) {
                            if (TECH_TABLE[t].bldgReq == b.type &&
                                !tech_is_researched(gameState, 0, t) &&
                                gameState.players[0].age >= TECH_TABLE[t].ageReq) {
                                if (tech_start_research(gameState, 0, t)) {
                                    didAction = true;
                                    break;
                                }
                            }
                        }
                    }

                    // Train selected unit type (or first available if none selected)
                    if (!didAction) {
                        if (gameState.trainUnitType >= 0 &&
                            UNIT_STATS[gameState.trainUnitType].bldgReq == b.type &&
                            gameState.players[0].age >= UNIT_STATS[gameState.trainUnitType].ageReq) {
                            building_train(gameState.selectedBldg, gameState.trainUnitType, gameState);
                        } else {
                            for (int ut = 0; ut < UNIT_TYPE_COUNT; ut++) {
                                if (UNIT_STATS[ut].bldgReq == b.type &&
                                    gameState.players[0].age >= UNIT_STATS[ut].ageReq) {
                                    building_train(gameState.selectedBldg, ut, gameState);
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Game logic
        units_update(gameState, terrain);
        buildings_update(gameState, terrain);
        projectiles_update(gameState);
        game_update(gameState);

        // Fog of war (every other frame for performance)
        if ((gameState.frameCount & 1) == 0) {
            fogMap.update();
        }

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
        terrain.renderViewport(terrainBuf, gameState.camX, gameState.camY);

        // Sub screen: fog overlay on buffer (isometric diamond tiles)
        {
            // Determine visible tile range from screen corners
            int minTX, minTY, maxTX, maxTY;
            int tmpTX, tmpTY;

            screenToTile(0, 0, gameState.camX, gameState.camY, minTX, minTY);
            maxTX = minTX; maxTY = minTY;

            screenToTile(SCREEN_W, 0, gameState.camX, gameState.camY, tmpTX, tmpTY);
            if (tmpTX < minTX) minTX = tmpTX;
            if (tmpTX > maxTX) maxTX = tmpTX;
            if (tmpTY < minTY) minTY = tmpTY;
            if (tmpTY > maxTY) maxTY = tmpTY;

            screenToTile(0, SCREEN_H, gameState.camX, gameState.camY, tmpTX, tmpTY);
            if (tmpTX < minTX) minTX = tmpTX;
            if (tmpTX > maxTX) maxTX = tmpTX;
            if (tmpTY < minTY) minTY = tmpTY;
            if (tmpTY > maxTY) maxTY = tmpTY;

            screenToTile(SCREEN_W, SCREEN_H, gameState.camX, gameState.camY, tmpTX, tmpTY);
            if (tmpTX < minTX) minTX = tmpTX;
            if (tmpTX > maxTX) maxTX = tmpTX;
            if (tmpTY < minTY) minTY = tmpTY;
            if (tmpTY > maxTY) maxTY = tmpTY;

            minTX -= 1; minTY -= 1;
            maxTX += 1; maxTY += 1;

            int minSum = minTX + minTY;
            int maxSum = maxTX + maxTY;

            for (int sum = minSum; sum <= maxSum; sum++) {
                for (int tx = minTX; tx <= maxTX; tx++) {
                    int ty = sum - tx;
                    if (ty < minTY || ty > maxTY) continue;
                    if (tx < 0 || tx >= MAP_TILES || ty < 0 || ty >= MAP_TILES) continue;

                    u8 fogState = fogMap.state[0][ty][tx];
                    if (fogState == FOG_VISIBLE) continue;

                    int isoFX, isoFY;
                    tileToIso(tx, ty, isoFX, isoFY);
                    int dstX = isoFX - gameState.camX;
                    int dstY = isoFY - gameState.camY;

                    if (dstX + ISO_TILE_W <= 0 || dstX >= SCREEN_W) continue;
                    if (dstY + ISO_TILE_H <= 0 || dstY >= SCREEN_H) continue;

                    for (int py = 0; py < ISO_TILE_H; py++) {
                        int screenY = dstY + py;
                        if (screenY < 0 || screenY >= SCREEN_H) continue;

                        int xs = ISO_DIAMOND_XSTART[py];
                        int xe = ISO_DIAMOND_XEND[py];

                        // Match the extended diamond used in renderViewport
                        if (!terrain.showTileGrid) {
                            if (xs > 0) xs--;
                            if (xe < ISO_TILE_W) xe++;
                        }

                        for (int px = xs; px < xe; px++) {
                            int screenX = dstX + px;
                            if (screenX < 0 || screenX >= SCREEN_W) continue;
                            int idx = screenY * 256 + screenX;
                            if (fogState == FOG_UNEXPLORED) {
                                terrainBuf[idx] = PAL_BLACK;
                            } else {
                                // Explored but not visible: checkerboard dither
                                if ((px + py) & 1) {
                                    terrainBuf[idx] = PAL_BLACK;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Sub screen: software-render units and buildings into buffer
        render_sprites_sw(terrainBuf, gameState, terrain);

        // Sub screen: move target marker
        render_move_target(terrainBuf, gameState);

        // Sub screen: attack visualization lines
        render_projectiles(terrainBuf, gameState);

        // Sub screen: training progress bars
        render_training_bars(terrainBuf, gameState);

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

        // DMA copy completed buffer to VRAM
        dmaCopy(terrainBuf, subVram, 256 * 192);

        // Top screen: minimap + info panel
        ui_update(gameState, terrain);

        // Stream music from NitroFS (manual mode)
        sound_music_update();

        swiWaitForVBlank();
    }

    return 0;
}
