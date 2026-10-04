#include "input.h"
#include "render.h"
#include "fog.h"
#include "game.h"
#include "units.h"
#include "buildings.h"
#include "terrain.h"
#include "iso.h"
#include "tech.h"
#include "sound.h"

// Drag threshold in pixels — beyond this, touch becomes a drag-select
enum { DRAG_THRESHOLD = 8 };

// ---------------------------------------------------------------------------
// Process a single tap at screen position (called on touch release if not drag)
// ---------------------------------------------------------------------------
static void process_tap(GameState& gs, TerrainMap& terrain, int screenX, int screenY, bool queue = false) {
    // Build menu check (bottom strip)
    if (gs.buildMenuOpen && screenY >= BUILD_MENU_Y) {
        int slot = screenX / BUILD_MENU_ITEM_W;
        if (slot >= BUILD_MENU_SLOTS) {
            // Last slot turns the page
            gs.buildMenuPage = (gs.buildMenuPage + 1) % BUILD_MENU_PAGES;
            return;
        }
        int bldgIdx = slot + gs.buildMenuPage * BUILD_MENU_SLOTS;
        if (bldgIdx < BLDG_TYPE_COUNT) {
            if (gs.players[0].age >= BLDG_STATS[bldgIdx].ageReq) {
                gs.inputMode = 1;
                gs.placeBldgType = bldgIdx;
                gs.buildMenuOpen = false;
            }
        }
        return;
    }

    // Train menu check (bottom strip when military building selected)
    if (screenY >= BUILD_MENU_Y && gs.selectionCount == 0 &&
        gs.selectedBldg >= 0 && !gs.buildMenuOpen) {
        const Building& b = buildings[gs.selectedBldg];
        if (b.alive && b.owner == 0 && building_is_complete(gs.selectedBldg)) {
            // Same slot order as render_train_menu; a locked unit is shown
            // but can't be picked
            int trainable[UNIT_TYPE_COUNT];
            int trainCount = 0;
            for (int ut = 0; ut < UNIT_TYPE_COUNT; ut++) {
                if (UNIT_STATS[ut].bldgReq == b.type && UNIT_STATS[ut].trainTime != 0)
                    trainable[trainCount++] = ut;
            }
            int slot = screenX / BUILD_MENU_ITEM_W;
            if (slot < trainCount && gs.players[0].age >= UNIT_STATS[trainable[slot]].ageReq) {
                gs.trainUnitType = trainable[slot];
                building_train(gs.selectedBldg, trainable[slot], gs);
            }
            return;
        }
    }

    // Convert screen coords to tile coords via isometric projection
    int tileX, tileY;
    screenToTile(screenX, screenY, gs.camX, gs.camY, tileX, tileY);
    int mapX = tileX * TILE_PX + TILE_PX / 2;
    int mapY = tileY * TILE_PX + TILE_PX / 2;

    if (gs.inputMode == 1) {
        // Placing building mode: the footprint is centred under the touch,
        // exactly where the preview showed it
        int originX, originY;
        placementOrigin(screenX, screenY, gs.camX, gs.camY,
                        BLDG_STATS[gs.placeBldgType].tileW, BLDG_STATS[gs.placeBldgType].tileH,
                        originX, originY);
        int result = building_place(gs.placeBldgType, 0, originX, originY, gs, terrain);
        if (result >= 0) {
            gs.inputMode = 0;
            // Send all selected villagers to build
            bool sentBuilder = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                if (gs.unitSelected[i] && units[i].alive && units[i].type == UNIT_VILLAGER) {
                    unit_command_build(i, result, terrain);
                    sentBuilder = true;
                }
            }
            if (!sentBuilder) {
                game_clear_selection(gs);
                gs.selectedBldg = result;
            }
        }
        return;
    }

    // Check if tapped on own unit (by its sprite). With units selected, a
    // sheep carcass that still has meat counts too, so villagers can be sent
    // back to it.
    int tappedUnit = render_pick_unit(gs, screenX, screenY, 0, gs.selectionCount > 0);
    if (tappedUnit >= 0 && units[tappedUnit].state == USTATE_DEAD) {
        for (int i = 0; i < MAX_UNITS; i++) {
            if (gs.unitSelected[i] && units[i].alive && units[i].type == UNIT_VILLAGER)
                unit_command_attack(i, tappedUnit);
        }
        return;
    }

    if (tappedUnit >= 0) {
        // If villagers selected and tapped on own sheep → gather food from sheep
        if (units[tappedUnit].type == UNIT_SHEEP && gs.selectionCount > 0) {
            bool sentGatherer = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                if (gs.unitSelected[i] && units[i].alive && units[i].type == UNIT_VILLAGER) {
                    if (queue)
                        unit_queue_command(i, Unit::CMD_ATTACK, 0, 0, tappedUnit);
                    else
                        unit_command_attack(i, tappedUnit);
                    sentGatherer = true;
                }
            }
            if (sentGatherer) return;
        }

        // Double-tap: if tapping already-selected unit, select all visible of same type
        if (gs.unitSelected[tappedUnit]) {
            u8 targetType = units[tappedUnit].type;
            game_clear_selection(gs);
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
                if (units[i].owner != 0 || units[i].type != targetType) continue;
                // Check if on screen
                int uIsoX2, uIsoY2;
                worldToIso(units[i].x, units[i].y, uIsoX2, uIsoY2);
                int sx2 = uIsoX2 - gs.camX;
                int sy2 = uIsoY2 - gs.camY;
                if (sx2 >= -32 && sx2 < SCREEN_W + 32 && sy2 >= -32 && sy2 < SCREEN_H + 32) {
                    game_add_to_selection(gs, i);
                }
            }
        } else {
            game_select_unit(gs, tappedUnit);
        }
        // Villager selection → random Britons voice; others → click
        if (units[tappedUnit].type == UNIT_VILLAGER)
            sound_play_random(SFX_VILL_SEL_FIRST, SFX_VILL_SEL_COUNT);
        else
            sound_play_random(SFX_MIL_SEL_FIRST, SFX_MIL_SEL_COUNT);
        return;
    }

    // Check if tapped on own building
    int tappedBldg = render_pick_building(gs, screenX, screenY);
    if (tappedBldg >= 0 && buildings[tappedBldg].owner == 0) {
        // If villagers selected and building incomplete or damaged, send all to build/repair
        if (gs.selectionCount > 0 &&
            (!building_is_complete(tappedBldg) ||
             buildings[tappedBldg].hp < BLDG_STATS[buildings[tappedBldg].type].hp)) {
            bool sentBuilder = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!gs.unitSelected[i]) continue;
                if (units[i].alive && units[i].type == UNIT_VILLAGER) {
                    unit_command_build(i, tappedBldg, terrain);
                    sentBuilder = true;
                }
            }
            if (sentBuilder) return;
        }
        // If units selected and tapping on TC, garrison them
        if (gs.selectionCount > 0 && buildings[tappedBldg].type == BLDG_TOWN_CENTER &&
            building_is_complete(tappedBldg)) {
            bool commanded = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!gs.unitSelected[i] || !units[i].alive) continue;
                if (units[i].type == UNIT_SHEEP) continue;
                unit_command_garrison(i, tappedBldg, terrain);
                commanded = true;
            }
            if (commanded) {
                game_clear_selection(gs);
                gs.selectedBldg = tappedBldg;
                return;
            }
        }
        game_clear_selection(gs);
        gs.selectedBldg = tappedBldg;
        return;
    }

    // Check if tapped on a visible enemy unit (attack command)
    int enemyUnit = render_pick_unit(gs, screenX, screenY, 1);
    if (enemyUnit >= 0) {
        int etx = (units[enemyUnit].x + TILE_PX / 2) / TILE_PX;
        int ety = (units[enemyUnit].y + TILE_PX / 2) / TILE_PX;
        if (!fogMap.isVisible(0, etx, ety)) enemyUnit = -1;
    }
    if (enemyUnit >= 0 && gs.selectionCount > 0) {
        for (int i = 0; i < MAX_UNITS; i++) {
            if (gs.unitSelected[i] && units[i].alive) {
                if (queue)
                    unit_queue_command(i, Unit::CMD_ATTACK, 0, 0, enemyUnit);
                else
                    unit_command_attack(i, enemyUnit);
            }
        }
        return;
    }

    // Check if tapped on enemy building (attack command)
    if (tappedBldg >= 0 && buildings[tappedBldg].owner != 0 && gs.selectionCount > 0) {
        for (int i = 0; i < MAX_UNITS; i++) {
            if (gs.unitSelected[i] && units[i].alive) {
                if (queue)
                    unit_queue_command(i, Unit::CMD_ATTACK_BLDG, 0, 0, tappedBldg);
                else
                    unit_command_attack_building(i, tappedBldg);
            }
        }
        return;
    }

    // Check if tapped on resource tile (gather command for selected villagers)
    if (gs.selectionCount > 0) {
        u8 tt = terrain.tileAt(tileX, tileY);
        if (tt == TERRAIN_FOREST || tt == TERRAIN_GOLD || tt == TERRAIN_STONE ||
            tt == TERRAIN_FARM || tt == TERRAIN_BERRIES) {
            bool sentGatherer = false;
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!gs.unitSelected[i]) continue;
                if (units[i].alive && units[i].type == UNIT_VILLAGER) {
                    if (queue)
                        unit_queue_command(i, Unit::CMD_GATHER, tileX, tileY);
                    else
                        unit_command_gather(i, tileX, tileY, terrain);
                    sentGatherer = true;
                }
            }
            if (sentGatherer) return;
        }
    }

    // Check if tapped on resource tile without selection — show info
    {
        u8 tt = terrain.tileAt(tileX, tileY);
        if (tt == TERRAIN_FOREST || tt == TERRAIN_GOLD || tt == TERRAIN_STONE ||
            tt == TERRAIN_FARM || tt == TERRAIN_BERRIES) {
            // If building selected that can train, set rally to resource
            if (gs.selectionCount == 0 && gs.selectedBldg >= 0 &&
                buildings[gs.selectedBldg].alive && buildings[gs.selectedBldg].owner == 0 &&
                building_is_complete(gs.selectedBldg)) {
                buildings[gs.selectedBldg].rallyTX = tileX;
                buildings[gs.selectedBldg].rallyTY = tileY;
                // Show rally marker
                int mtIsoX, mtIsoY;
                worldToIso(mapX, mapY, mtIsoX, mtIsoY);
                gs.moveTargetIsoX = mtIsoX;
                gs.moveTargetIsoY = mtIsoY;
                gs.moveTargetTimer = 30;
                return;
            }
            game_clear_selection(gs);
            gs.selectedTileX = tileX;
            gs.selectedTileY = tileY;
            return;
        }
    }

    // Set rally point: if a building is selected (no units), tap on map sets rally
    if (gs.selectionCount == 0 && gs.selectedBldg >= 0 &&
        buildings[gs.selectedBldg].alive && buildings[gs.selectedBldg].owner == 0 &&
        building_is_complete(gs.selectedBldg)) {
        buildings[gs.selectedBldg].rallyTX = tileX;
        buildings[gs.selectedBldg].rallyTY = tileY;
        // Show rally marker
        int mtIsoX, mtIsoY;
        worldToIso(mapX, mapY, mtIsoX, mtIsoY);
        gs.moveTargetIsoX = mtIsoX;
        gs.moveTargetIsoY = mtIsoY;
        gs.moveTargetTimer = 30;
        return;
    }

    // Default: move selected units with formation spreading.
    // Instead of sending all units to the same tile (causing stacking),
    // assign each unit a unique destination in a spiral pattern around
    // the tap point: center tile first, then ring-1 (8 tiles), ring-2 (16).
    // Impassable or already-assigned offsets are skipped.
    if (gs.selectionCount > 0) {
        gs.selectedTileX = -1;
        gs.selectedTileY = -1;
        sound_play_random(SFX_VILL_CMD_FIRST, SFX_VILL_CMD_COUNT);

        // Set move target marker
        int mtIsoX, mtIsoY;
        worldToIso(mapX, mapY, mtIsoX, mtIsoY);
        gs.moveTargetIsoX = mtIsoX;
        gs.moveTargetIsoY = mtIsoY;
        gs.moveTargetTimer = 30; // 0.5s at 60fps

        int centerTX = mapX / TILE_PX;
        int centerTY = mapY / TILE_PX;

        // Build spiral offset table: center, then ring-1, ring-2
        static const int MAX_OFFSETS = 25; // 1 + 8 + 16
        int offX[MAX_OFFSETS], offY[MAX_OFFSETS];
        int numOffsets = 0;
        offX[numOffsets] = 0; offY[numOffsets] = 0; numOffsets++;
        for (int r = 1; r <= 2 && numOffsets < MAX_OFFSETS; r++) {
            for (int dy = -r; dy <= r && numOffsets < MAX_OFFSETS; dy++) {
                for (int dx = -r; dx <= r && numOffsets < MAX_OFFSETS; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    // Only tiles on this ring (not inner rings)
                    int adx = dx < 0 ? -dx : dx;
                    int ady = dy < 0 ? -dy : dy;
                    if (adx < r && ady < r) continue;
                    offX[numOffsets] = dx;
                    offY[numOffsets] = dy;
                    numOffsets++;
                }
            }
        }

        // Mark which offsets are already assigned
        bool assigned[MAX_OFFSETS];
        memset(assigned, 0, sizeof(assigned));

        int offsetIdx = 0;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (!gs.unitSelected[i] || !units[i].alive) continue;

            // Find next available offset tile
            int destTX = centerTX, destTY = centerTY;
            for (int o = offsetIdx; o < numOffsets; o++) {
                int tx = centerTX + offX[o];
                int ty = centerTY + offY[o];
                if (tx >= 0 && tx < MAP_TILES && ty >= 0 && ty < MAP_TILES &&
                    terrain.passable(tx, ty) && !assigned[o]) {
                    destTX = tx;
                    destTY = ty;
                    assigned[o] = true;
                    offsetIdx = o + 1;
                    break;
                }
            }

            if (queue)
                unit_queue_command(i, Unit::CMD_MOVE,
                                   destTX * TILE_PX + TILE_PX / 2,
                                   destTY * TILE_PX + TILE_PX / 2);
            else
                unit_command_move(i, destTX * TILE_PX + TILE_PX / 2,
                                  destTY * TILE_PX + TILE_PX / 2, terrain);
        }
    }
}

// ---------------------------------------------------------------------------
// Process drag-select: select all own units within the screen-space box
// ---------------------------------------------------------------------------
static void process_drag_select(GameState& gs, int x0, int y0, int x1, int y1) {
    // Normalize box
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }

    // Select all own units in the box
    game_clear_selection(gs);
    for (int i = 0; i < MAX_UNITS; i++) {
        if (!units[i].alive || units[i].state == USTATE_DEAD) continue;
        if (units[i].state == USTATE_GARRISONED) continue;  // inside a building
        if (units[i].owner != 0) continue;
        if (units[i].type == UNIT_SHEEP) continue;  // livestock isn't boxed up with the army
        int uIsoX, uIsoY;
        worldToIso(units[i].x, units[i].y, uIsoX, uIsoY);
        int usx = uIsoX - gs.camX + 16; // center of 32px sprite
        int usy = uIsoY - gs.camY;       // feet position
        if (usx >= x0 && usx <= x1 && usy >= y0 && usy <= y1) {
            game_add_to_selection(gs, i);
        }
    }
}

void input_update(GameState& gs, TerrainMap& terrain) {
    scanKeys();
    int keys = keysHeld();
    int keysPressed = keysDown();
    int keysReleased = keysUp();

    touchPosition touch;
    touchRead(&touch);
    bool touchDown = (keys & KEY_TOUCH) != 0;
    bool touchPressed = (keysPressed & KEY_TOUCH) != 0;
    bool touchReleased = (keysReleased & KEY_TOUCH) != 0;

    // SELECT = cycle stance (if military selected), else toggle follow cam
    if (keysPressed & KEY_SELECT) {
        bool didStance = false;
        if (gs.selectionCount > 0) {
            for (int i = 0; i < MAX_UNITS; i++) {
                if (!gs.unitSelected[i] || !units[i].alive) continue;
                if (units[i].type != UNIT_VILLAGER && units[i].type != UNIT_SHEEP) {
                    units[i].stance = (units[i].stance + 1) % 4;
                    didStance = true;
                }
            }
        }
        if (!didStance) gs.followCam = !gs.followCam;
    }

    // START = toggle music (when no building selected; training handled in main.cpp)
    // When market selected: execute trade
    if (keysPressed & KEY_START) {
        if (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
            buildings[gs.selectedBldg].type == BLDG_MARKET &&
            buildings[gs.selectedBldg].owner == 0 &&
            building_is_complete(gs.selectedBldg)) {
            // Market trade: sell 100 of one resource, buy 70 of another
            static const int TRADE_SELL_RES[4] = { RES_FOOD, RES_WOOD, RES_GOLD, RES_STONE };
            static const int TRADE_BUY_RES[4]  = { RES_GOLD, RES_FOOD, RES_STONE, RES_WOOD };
            int sellRes = TRADE_SELL_RES[gs.marketTradeIdx];
            int buyRes  = TRADE_BUY_RES[gs.marketTradeIdx];
            if (gs.players[0].resources[sellRes] >= 100) {
                gs.players[0].resources[sellRes] -= 100;
                gs.players[0].resources[buyRes]  += 70;
            }
        } else if (gs.selectedBldg < 0) {
            sound_music_toggle();
        }
    }

    // D-pad: camera scroll, or market trade cycling when market selected
    bool marketSelected = (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
                           buildings[gs.selectedBldg].type == BLDG_MARKET &&
                           buildings[gs.selectedBldg].owner == 0 &&
                           building_is_complete(gs.selectedBldg));
    // Check if building has multiple trainable unit types (D-pad up/down used for cycling)
    bool trainCycleActive = false;
    if (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
        buildings[gs.selectedBldg].owner == 0 &&
        building_is_complete(gs.selectedBldg) && !marketSelected) {
        int trainCount = 0;
        for (int ut = 0; ut < UNIT_TYPE_COUNT; ut++) {
            if (UNIT_STATS[ut].bldgReq == buildings[gs.selectedBldg].type &&
                gs.players[0].age >= UNIT_STATS[ut].ageReq) trainCount++;
        }
        trainCycleActive = (trainCount > 1);
    }
    if (marketSelected) {
        if (keysPressed & KEY_LEFT) {
            gs.marketTradeIdx = (gs.marketTradeIdx + 3) % 4; // wrap backward
        }
        if (keysPressed & KEY_RIGHT) {
            gs.marketTradeIdx = (gs.marketTradeIdx + 1) % 4;
        }
    }
    if (!gs.followCam && !marketSelected) {
        // Double-tap-and-hold same direction: fast scroll at 12px/frame
        static u8 dpadGapTimer = 0;  // frames since last d-pad release
        static int dpadPrevDir = 0;  // d-pad direction held last frame
        static int dpadLastDir = 0;  // direction that was released
        static bool dpadFast = false;
        int dpadMask = KEY_UP | KEY_DOWN | KEY_LEFT | KEY_RIGHT;
        int dpadHeld = keys & dpadMask;
        int dpadJustPressed = keysPressed & dpadMask;
        bool dpadJustReleased = (keysReleased & dpadMask) != 0;

        if (dpadJustReleased) { dpadLastDir = dpadPrevDir; dpadGapTimer = 1; }
        else if (dpadGapTimer > 0 && !dpadHeld) dpadGapTimer++;

        if (dpadJustPressed && dpadGapTimer > 0 && dpadGapTimer <= 6 &&
            dpadJustPressed == dpadLastDir)
            dpadFast = true;
        if (!dpadHeld) {
            if (dpadGapTimer > 6) { dpadGapTimer = 0; dpadFast = false; }
        }
        if (dpadHeld && dpadFast && !(dpadHeld & dpadLastDir))
            dpadFast = false;

        dpadPrevDir = dpadHeld;

        int scrollSpeed = dpadFast ? 12 : 6;
        if (keys & KEY_UP)    { if (!trainCycleActive) gs.camY -= scrollSpeed; }
        if (keys & KEY_DOWN)  { if (!trainCycleActive) gs.camY += scrollSpeed; }
        if (keys & KEY_LEFT)  gs.camX -= scrollSpeed;
        if (keys & KEY_RIGHT) gs.camX += scrollSpeed;
    }

    // L/R: cycle idle villagers
    if (keysPressed & KEY_L) {
        int start = (gs.selectedUnit >= 0) ? gs.selectedUnit + 1 : 0;
        int vil = unit_find_idle_villager(0, start);
        if (vil >= 0) {
            game_select_unit(gs, vil);
            gs.followCam = true;
        }
    }
    // R: cycle idle villagers (only when not touching — R held = queue modifier)
    if ((keysPressed & KEY_R) && !touchDown) {
        int vil = unit_find_idle_villager(0, 0);
        if (vil >= 0) {
            game_select_unit(gs, vil);
            gs.followCam = true;
        }
    }

    // A: ungarrison TC if selected and has garrison, else select first military unit
    if (keysPressed & KEY_A) {
        if (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
            buildings[gs.selectedBldg].type == BLDG_TOWN_CENTER &&
            buildings[gs.selectedBldg].garrisonCount > 0) {
            building_ungarrison_all(gs.selectedBldg, terrain);
        } else {
            for (int i = 0; i < MAX_UNITS; i++) {
                if (units[i].alive && units[i].owner == 0 && unit_is_military(units[i].type) &&
                    units[i].state != USTATE_DEAD && units[i].state != USTATE_GARRISONED) {
                    game_select_unit(gs, i);
                    break;
                }
            }
        }
    }

    // B: cancel current action, or cancel building training
    if (keysPressed & KEY_B) {
        if (gs.inputMode != 0) {
            gs.inputMode = 0;
            gs.buildMenuOpen = false;
        } else if (gs.selectionCount > 0) {
            // Nothing in progress: B lets go of the selected units, so the
            // next tap on a building selects it instead of sending them in
            game_clear_selection(gs);
            gs.buildMenuOpen = false;
        } else if (gs.selectedBldg >= 0 && buildings[gs.selectedBldg].alive &&
                   buildings[gs.selectedBldg].trainQueue[0] >= 0) {
            building_cancel_train(gs.selectedBldg, gs);
        }
    }

    // X: toggle/cycle build menu pages
    if (keysPressed & KEY_X) {
        if (gs.buildMenuOpen) {
            gs.buildMenuPage = (gs.buildMenuPage + 1) % BUILD_MENU_PAGES;
            if (gs.buildMenuPage == 0) {
                gs.buildMenuOpen = false;
            }
        } else {
            gs.buildMenuOpen = true;
            gs.buildMenuPage = 0;
        }
        gs.inputMode = 0;
    }

    // Y: center on TC (only when not touching — Y held = patrol modifier)
    if ((keysPressed & KEY_Y) && !touchDown) {
        int tc = building_nearest(0, BLDG_TOWN_CENTER, 0, 0);
        if (tc >= 0) {
            int isoX, isoY;
            worldToIso(buildings[tc].x, buildings[tc].y, isoX, isoY);
            gs.camX = isoX - SCREEN_W / 2;
            gs.camY = isoY - SCREEN_H / 2;
            gs.followCam = false;
        }
    }

    // --- Touch state machine ---
    if (gs.phase == PHASE_PLAYING) {
        if (touchPressed) {
            // Touch just started — record start position
            gs.touchActive = true;
            gs.isDragging = false;
            gs.dragStartX = touch.px;
            gs.dragStartY = touch.py;
            gs.dragEndX = touch.px;
            gs.dragEndY = touch.py;
        } else if (touchDown && gs.touchActive) {
            // Touch held — update end position and check for drag
            gs.dragEndX = touch.px;
            gs.dragEndY = touch.py;
            int dx = gs.dragEndX - gs.dragStartX;
            int dy = gs.dragEndY - gs.dragStartY;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            // While placing a building a drag moves the building, it doesn't
            // start a selection box
            if (gs.inputMode != 1 && (dx > DRAG_THRESHOLD || dy > DRAG_THRESHOLD)) {
                gs.isDragging = true;
            }
        } else if (touchReleased && gs.touchActive) {
            // Touch released — process action
            gs.touchActive = false;
            if (gs.inputMode == 1) {
                // Drop the building where the stylus was lifted
                process_tap(gs, terrain, gs.dragEndX, gs.dragEndY);
            } else if (gs.isDragging) {
                process_drag_select(gs, gs.dragStartX, gs.dragStartY,
                                    gs.dragEndX, gs.dragEndY);
            } else {
                bool queueCmd = (keys & KEY_R) != 0;
                bool patrolCmd = (keys & KEY_Y) != 0;
                if (patrolCmd && gs.selectionCount > 0) {
                    // Patrol: Y held + tap = patrol between current pos and target
                    int tileX, tileY;
                    screenToTile(gs.dragStartX, gs.dragStartY, gs.camX, gs.camY, tileX, tileY);
                    int mapX = tileX * TILE_PX + TILE_PX / 2;
                    int mapY = tileY * TILE_PX + TILE_PX / 2;
                    for (int i = 0; i < MAX_UNITS; i++) {
                        if (gs.unitSelected[i] && units[i].alive)
                            unit_command_patrol(i, mapX, mapY, terrain);
                    }
                } else {
                    process_tap(gs, terrain, gs.dragStartX, gs.dragStartY, queueCmd);
                }
            }
            gs.isDragging = false;
        } else if (!touchDown) {
            gs.touchActive = false;
            gs.isDragging = false;
        }
    }

    // Auto-show build menu when any selected unit is a villager
    if (gs.inputMode == 0) {
        bool villagerSelected = false;
        for (int i = 0; i < MAX_UNITS; i++) {
            if (gs.unitSelected[i] && units[i].alive &&
                units[i].owner == 0 && units[i].type == UNIT_VILLAGER) {
                villagerSelected = true;
                break;
            }
        }
        gs.buildMenuOpen = villagerSelected;
    }

    // Follow camera on selected unit (use iso position)
    if (gs.followCam && gs.selectedUnit >= 0 && units[gs.selectedUnit].alive) {
        int isoX, isoY;
        worldToIso(units[gs.selectedUnit].x, units[gs.selectedUnit].y, isoX, isoY);
        gs.camX = isoX - SCREEN_W / 2;
        gs.camY = isoY - SCREEN_H / 2;
    }

    // Clamp camera to isometric map bounds
    if (gs.camX < 0) gs.camX = 0;
    if (gs.camY < 0) gs.camY = 0;
    if (gs.camX > ISO_MAP_W - SCREEN_W) gs.camX = ISO_MAP_W - SCREEN_W;
    if (gs.camY > ISO_MAP_H - SCREEN_H) gs.camY = ISO_MAP_H - SCREEN_H;
}
