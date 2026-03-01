#include "input.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "tech.h"

// Build menu layout on bottom screen (bottom strip)
enum { BUILD_MENU_Y = 176, BUILD_MENU_H = 16, BUILD_MENU_ITEM_W = 32 };

void input_update(GameState& gs, TerrainMap& terrain) {
    scanKeys();
    int keys = keysHeld();
    int keysPressed = keysDown();

    touchPosition touch;
    touchRead(&touch);
    bool touchDown = (keys & KEY_TOUCH) != 0;
    bool touchPressed = (keysPressed & KEY_TOUCH) != 0;
    (void)touchDown;

    // START = pause (not implemented beyond exit for now)
    // SELECT = toggle follow cam
    if (keysPressed & KEY_SELECT) gs.followCam = !gs.followCam;

    // D-pad: camera scroll
    if (!gs.followCam) {
        int scrollSpeed = 2;
        if (keys & KEY_UP)    gs.camY -= scrollSpeed;
        if (keys & KEY_DOWN)  gs.camY += scrollSpeed;
        if (keys & KEY_LEFT)  gs.camX -= scrollSpeed;
        if (keys & KEY_RIGHT) gs.camX += scrollSpeed;
    }

    // L/R: cycle idle villagers
    if (keysPressed & KEY_L) {
        int start = (gs.selectedUnit >= 0) ? gs.selectedUnit + 1 : 0;
        int vil = unit_find_idle_villager(0, start);
        if (vil >= 0) {
            gs.selectedUnit = vil;
            gs.selectedBldg = -1;
            gs.followCam = true;
        }
    }
    if (keysPressed & KEY_R) {
        // Cycle backwards (just find any idle)
        int vil = unit_find_idle_villager(0, 0);
        if (vil >= 0) {
            gs.selectedUnit = vil;
            gs.selectedBldg = -1;
            gs.followCam = true;
        }
    }

    // A: select all military units (toggle)
    if (keysPressed & KEY_A) {
        // Select first military unit
        for (int i = 0; i < MAX_UNITS; i++) {
            if (units[i].alive && units[i].owner == 0 && units[i].type != UNIT_VILLAGER &&
                units[i].state != USTATE_DEAD) {
                gs.selectedUnit = i;
                gs.selectedBldg = -1;
                break;
            }
        }
    }

    // B: cancel current action
    if (keysPressed & KEY_B) {
        gs.inputMode = 0;
        gs.buildMenuOpen = false;
    }

    // X: toggle build menu
    if (keysPressed & KEY_X) {
        gs.buildMenuOpen = !gs.buildMenuOpen;
        gs.inputMode = 0;
    }

    // Y: center on TC
    if (keysPressed & KEY_Y) {
        int tc = building_nearest(0, BLDG_TOWN_CENTER, 0, 0);
        if (tc >= 0) {
            gs.camX = buildings[tc].x - SCREEN_W / 2;
            gs.camY = buildings[tc].y - SCREEN_H / 2;
            gs.followCam = false;
        }
    }

    // Touch input
    if (touchPressed && gs.phase == PHASE_PLAYING) {
        int screenX = touch.px;
        int screenY = touch.py;

        // Build menu check (bottom strip)
        if (gs.buildMenuOpen && screenY >= BUILD_MENU_Y) {
            int slot = screenX / BUILD_MENU_ITEM_W;
            if (slot < BLDG_TYPE_COUNT) {
                // Check if player can build this
                if (gs.players[0].age >= BLDG_STATS[slot].ageReq) {
                    gs.inputMode = 1; // placing building
                    gs.placeBldgType = slot;
                    gs.buildMenuOpen = false;
                }
            }
            return;
        }

        // Convert screen coords to map coords
        int mapX = screenX + gs.camX;
        int mapY = screenY + gs.camY;

        if (gs.inputMode == 1) {
            // Placing building mode
            int tileX = mapX / TILE_PX;
            int tileY = mapY / TILE_PX;

            int result = building_place(gs.placeBldgType, 0, tileX, tileY, gs, terrain);
            if (result >= 0) {
                gs.inputMode = 0;
                gs.selectedBldg = result;
                gs.selectedUnit = -1;
            }
            return;
        }

        // Normal touch: select unit, building, or issue command
        // Check if tapped on own unit
        int tappedUnit = -1;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
            if (units[i].owner != 0) continue;
            int dx = mapX - units[i].x;
            int dy = mapY - units[i].y;
            if (dx >= 0 && dx < TILE_PX && dy >= 0 && dy < TILE_PX) {
                tappedUnit = i;
                break;
            }
        }

        if (tappedUnit >= 0) {
            gs.selectedUnit = tappedUnit;
            gs.selectedBldg = -1;
            return;
        }

        // Check if tapped on own building
        int tileX = mapX / TILE_PX;
        int tileY = mapY / TILE_PX;
        int tappedBldg = building_at_tile(tileX, tileY);
        if (tappedBldg >= 0 && buildings[tappedBldg].owner == 0) {
            gs.selectedBldg = tappedBldg;
            gs.selectedUnit = -1;
            return;
        }

        // Check if tapped on enemy unit (attack command)
        int enemyUnit = unit_at_pixel(mapX, mapY, 0); // ignore player 0
        if (enemyUnit >= 0 && gs.selectedUnit >= 0) {
            unit_command_attack(gs.selectedUnit, enemyUnit);
            return;
        }

        // Check if tapped on enemy building (attack command)
        if (tappedBldg >= 0 && buildings[tappedBldg].owner != 0 && gs.selectedUnit >= 0) {
            unit_command_attack_building(gs.selectedUnit, tappedBldg);
            return;
        }

        // Check if tapped on resource tile (gather command for villager)
        if (gs.selectedUnit >= 0 && units[gs.selectedUnit].type == UNIT_VILLAGER) {
            u8 tt = terrain.tileAt(tileX, tileY);
            if (tt == TERRAIN_FOREST || tt == TERRAIN_GOLD || tt == TERRAIN_STONE || tt == TERRAIN_FARM) {
                unit_command_gather(gs.selectedUnit, tileX, tileY, terrain);
                return;
            }
        }

        // Default: move command
        if (gs.selectedUnit >= 0) {
            unit_command_move(gs.selectedUnit, mapX, mapY, terrain);
        }
    }

    // Follow camera on selected unit
    if (gs.followCam && gs.selectedUnit >= 0 && units[gs.selectedUnit].alive) {
        gs.camX = units[gs.selectedUnit].x - SCREEN_W / 2;
        gs.camY = units[gs.selectedUnit].y - SCREEN_H / 2;
    }

    // Clamp camera
    if (gs.camX < 0) gs.camX = 0;
    if (gs.camY < 0) gs.camY = 0;
    if (gs.camX > MAP_PX - SCREEN_W) gs.camX = MAP_PX - SCREEN_W;
    if (gs.camY > MAP_PX - SCREEN_H) gs.camY = MAP_PX - SCREEN_H;
}
