// AoE2 DSi — Age of Empires 2 for Nintendo DSi
// Main entry point and game loop
#include <nds.h>
#include <stdio.h>

#include "config.h"
#include "game.h"
#include "terrain.h"
#include "units.h"
#include "buildings.h"
#include "input.h"
#include "render.h"
#include "ui.h"
#include "fog.h"
#include "tech.h"
#include "ai.h"
#include "sound.h"

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

    // Generate map with a semi-random seed
    terrain.generate(12345);

    // Temporarily give enough resources to place starting TCs (free in AoE2)
    for (int p = 0; p < NUM_PLAYERS; p++) {
        gameState.players[p].resources[RES_WOOD]  = 9999;
        gameState.players[p].resources[RES_STONE] = 9999;
    }

    // --- Player 0 (human) — top-left corner ---
    // Place Town Center at tile (2,2)
    int tc0 = building_place(BLDG_TOWN_CENTER, 0, 2, 2, gameState, terrain);
    if (tc0 >= 0) {
        buildings[tc0].buildProgress = BLDG_STATS[BLDG_TOWN_CENTER].buildTime; // start complete
    }

    // Spawn 3 villagers near TC
    unit_spawn(UNIT_VILLAGER, 0, 4 * TILE_PX, 4 * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 0, 5 * TILE_PX, 4 * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 0, 4 * TILE_PX, 5 * TILE_PX);

    // --- Player 1 (AI) — bottom-right corner ---
    int tc1 = building_place(BLDG_TOWN_CENTER, 1, MAP_TILES - 4, MAP_TILES - 4, gameState, terrain);
    if (tc1 >= 0) {
        buildings[tc1].buildProgress = BLDG_STATS[BLDG_TOWN_CENTER].buildTime;
    }

    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 3) * TILE_PX, (MAP_TILES - 3) * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 2) * TILE_PX, (MAP_TILES - 3) * TILE_PX);
    unit_spawn(UNIT_VILLAGER, 1, (MAP_TILES - 3) * TILE_PX, (MAP_TILES - 2) * TILE_PX);

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

    // Center camera on player's TC
    gameState.camX = 2 * TILE_PX - SCREEN_W / 2;
    gameState.camY = 2 * TILE_PX - SCREEN_H / 2;
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
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    oamInit(&oamMain, SpriteMapping_1D_128, false);

    ui_init();  // sets up main engine video mode, VRAM_A, console

    // Load shared terrain palette into BG_PALETTE_SUB (includes UI colors at 0-15)
    terrain_initPalette();

    // Init terrain tile graphics cache from preprocessed binary data
    terrain.initTileGfx();

    // Init render system
    render_init();

    // Init sound (stubs for now)
    sound_init();

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

        // Sub screen: fog overlay on buffer
        {
            int startTX = gameState.camX / TILE_PX;
            int startTY = gameState.camY / TILE_PX;
            int offX = gameState.camX % TILE_PX;
            int offY = gameState.camY % TILE_PX;
            int tilesW = (SCREEN_W / TILE_PX) + 2;
            int tilesH = (SCREEN_H / TILE_PX) + 2;

            for (int ty = 0; ty < tilesH; ty++) {
                for (int tx = 0; tx < tilesW; tx++) {
                    int mapTX = startTX + tx;
                    int mapTY = startTY + ty;
                    if (mapTX < 0 || mapTX >= MAP_TILES || mapTY < 0 || mapTY >= MAP_TILES) continue;

                    u8 fogState = fogMap.state[0][mapTY][mapTX];
                    if (fogState == FOG_VISIBLE) continue;

                    int dstX = tx * TILE_PX - offX;
                    int dstY = ty * TILE_PX - offY;

                    for (int py = 0; py < TILE_PX; py++) {
                        int screenY = dstY + py;
                        if (screenY < 0 || screenY >= SCREEN_H) continue;
                        for (int px = 0; px < TILE_PX; px++) {
                            int screenX = dstX + px;
                            if (screenX < 0 || screenX >= SCREEN_W) continue;
                            int idx = screenY * 256 + screenX;
                            if (fogState == FOG_UNEXPLORED) {
                                terrainBuf[idx] = PAL_BLACK;
                            } else {
                                if ((px + py) & 1) {
                                    terrainBuf[idx] = PAL_BLACK;
                                }
                            }
                        }
                    }
                }
            }
        }

        // Sub screen: build menu overlay on buffer
        render_build_menu(terrainBuf, gameState);

        // DMA copy completed buffer to VRAM
        dmaCopy(terrainBuf, subVram, 256 * 192);

        // Sub screen: OAM sprites for units and buildings
        render_sprites(gameState, terrain);

        // Top screen: minimap + info panel
        ui_update(gameState, terrain);

        swiWaitForVBlank();
    }

    return 0;
}
