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
#include <filesystem.h>

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

    game_init(gameState);
    units_init();
    buildings_init();
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

    // --- Player 0 (human) — top-left corner ---
    // Place Town Center at tile (2,2) — 4x4 building
    int tc0 = building_place(BLDG_TOWN_CENTER, 0, 2, 2, gameState, terrain);
    if (tc0 >= 0) {
        buildings[tc0].buildProgress = BLDG_STATS[BLDG_TOWN_CENTER].buildTime; // start complete
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


    // --- Player 1 (AI) — bottom-right corner ---
    int tc1 = building_place(BLDG_TOWN_CENTER, 1, MAP_TILES - 6, MAP_TILES - 6, gameState, terrain);
    if (tc1 >= 0) {
        buildings[tc1].buildProgress = BLDG_STATS[BLDG_TOWN_CENTER].buildTime;
    }

    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 2) * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 1) * TILE_PX, (MAP_TILES - 2) * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 1) * TILE_PX);

    // Spawn AI scout near TC (auto-scouts by default)
    {
        int si = unit_spawn(UNIT_SCOUT, 1, (MAP_TILES - 1) * TILE_PX, (MAP_TILES - 1) * TILE_PX);
        if (si >= 0) units[si].state = USTATE_SCOUTING;
    }

    // Reset resources to actual starting values (TC placement was free)
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gameState.players[p].resources[RES_FOOD]  = 200;
        gameState.players[p].resources[RES_WOOD]  = 200;
        gameState.players[p].resources[RES_GOLD]  = 100;
        gameState.players[p].resources[RES_STONE] = 200;
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

    // Init NitroFS (for streaming music from ROM filesystem)
    nitroFSInit(NULL);

    // Init sound
    sound_init();

    // Start background music
    sound_music_start();

    // Start free-running hardware timers for random seed generation
    TIMER0_CR = TIMER_ENABLE | TIMER_DIV_1;
    TIMER1_CR = TIMER_ENABLE | TIMER_CASCADE;

    // Start first game
    game_start();

    // === Main Loop ===
    while (pmMainLoop()) {
        // Handle game-over touch-to-restart
        if (gameState.phase != PHASE_PLAYING) {
            scanKeys();
            if (keysDown() & KEY_TOUCH) {
                game_start();
            }
            // Still update UI to show victory/defeat
            ui_update(gameState, terrain);
            swiWaitForVBlank();
            continue;
        }

        // Input
        input_update(gameState, terrain);

        // Handle training/research from selected building via START
        // Note: scanKeys() already called in input_update() — reuse keysDown()
        {
            int kp = keysDown();
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

                    // Quick-train: train the first available unit
                    if (!didAction) {
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

        // Game logic
        units_update(gameState, terrain);
        buildings_update(gameState, terrain);
        game_update(gameState);

        // Fog of war (every other frame for performance)
        if ((gameState.frameCount & 1) == 0) {
            fogMap.update();
        }

        // AI
        ai_update(gameState, terrain);

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
        render_attack_lines(terrainBuf, gameState);

        // Sub screen: training progress bars
        render_training_bars(terrainBuf, gameState);

        // Sub screen: building placement preview (diamond footprint)
        render_placement_preview(terrainBuf, gameState, terrain);

        // Sub screen: build menu overlay on buffer
        render_build_menu(terrainBuf, gameState);

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
