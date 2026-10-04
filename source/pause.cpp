#include "pause.h"
#include "game.h"
#include "terrain.h"
#include "save.h"
#include "sound.h"
#include "font.h"
#include <stdio.h>
#include <string.h>

enum Page { PAGE_MAIN, PAGE_SAVE, PAGE_LOAD, PAGE_NEW };
enum { BOX_X = 24, BOX_W = 208, TITLE_Y = 10, ITEM_Y = 34, ITEM_PITCH = 24, ITEM_H = 20, MAX_ITEMS = 5 };

static bool open = false;
static bool loadOnly = false;     // opened from the game-over screen
static int page = PAGE_MAIN;
static int cursor = 0;
static const char* message = NULL;
static int messageTimer = 0;

struct Item { char label[36]; bool enabled; };

void pause_open(bool loadPage) {
    open = true;
    loadOnly = loadPage;
    page = loadPage ? PAGE_LOAD : PAGE_MAIN;
    cursor = 0;
    messageTimer = 0;
}

bool pause_is_open() {
    return open;
}

static void say(const char* text) {
    message = text;
    messageTimer = 120;
}

static void go(int newPage) {
    page = newPage;
    cursor = 0;
}

// "1  Castle  12:34  pop 25/30" or "1  (empty)"
static bool slot_label(int slot, char* out, size_t size) {
    static const char* const AGE[] = { "Dark", "Feudal", "Castle", "Imperial" };
    SaveInfo info;
    if (!save_info(slot, info)) {
        snprintf(out, size, "%d  (empty)", slot + 1);
        return false;
    }
    snprintf(out, size, "%d  %s  %d:%02d  pop %d", slot + 1, AGE[info.age & 3],
             info.frames / 3600, (info.frames / 60) % 60, info.pop);
    return true;
}

static int build_items(Item* items, const GameState& gs) {
    int n = 0;
    static const char* const DIFF[] = { "Easy", "Normal", "Hard" };
    switch (page) {
    case PAGE_MAIN:
        snprintf(items[n].label, sizeof(items[n].label), "Resume"); items[n++].enabled = true;
        snprintf(items[n].label, sizeof(items[n].label), "Save game"); items[n++].enabled = save_available();
        snprintf(items[n].label, sizeof(items[n].label), "Load game"); items[n++].enabled = save_available();
        snprintf(items[n].label, sizeof(items[n].label), "Music: %s", sound_music_playing() ? "on" : "off");
        items[n++].enabled = true;
        snprintf(items[n].label, sizeof(items[n].label), "New game"); items[n++].enabled = true;
        break;
    case PAGE_SAVE:
    case PAGE_LOAD:
        for (int s = 0; s < SAVE_SLOTS; s++) {
            bool used = slot_label(s, items[n].label, sizeof(items[n].label));
            items[n++].enabled = (page == PAGE_SAVE) || used;
        }
        snprintf(items[n].label, sizeof(items[n].label), "Back"); items[n++].enabled = true;
        break;
    case PAGE_NEW:
        snprintf(items[n].label, sizeof(items[n].label), "Start new game"); items[n++].enabled = true;
        snprintf(items[n].label, sizeof(items[n].label), "AI: %s", DIFF[gs.aiDifficulty % 3]);
        items[n++].enabled = true;
        snprintf(items[n].label, sizeof(items[n].label), "Back"); items[n++].enabled = true;
        break;
    }
    return n;
}

static PauseResult activate(int item, GameState& gs, TerrainMap& terrain) {
    switch (page) {
    case PAGE_MAIN:
        if (item == 0) { open = false; return PAUSE_RESUME; }
        if (item == 1) go(PAGE_SAVE);
        if (item == 2) go(PAGE_LOAD);
        if (item == 3) sound_music_toggle();
        if (item == 4) go(PAGE_NEW);
        break;
    case PAGE_SAVE:
        if (item < SAVE_SLOTS) {
            say(save_game(item, terrain) ? "Game saved." : "Could not save.");
            go(PAGE_MAIN);
        } else go(PAGE_MAIN);
        break;
    case PAGE_LOAD:
        if (item < SAVE_SLOTS) {
            if (load_game(item, terrain)) {
                open = false;
                return PAUSE_RESUME;
            }
            say("Could not load that game.");
        } else if (loadOnly) {
            open = false;
            return PAUSE_RESUME;
        } else go(PAGE_MAIN);
        break;
    case PAGE_NEW:
        if (item == 0) { open = false; return PAUSE_NEW_GAME; }
        if (item == 1) gs.aiDifficulty = (gs.aiDifficulty + 1) % 3;
        if (item == 2) go(PAGE_MAIN);
        break;
    }
    return PAUSE_STAY;
}

PauseResult pause_frame(u8* buf, GameState& gs, TerrainMap& terrain) {
    static const char* const TITLE[] = { "Paused", "Save game", "Load game", "New game" };
    Item items[MAX_ITEMS];
    int count = build_items(items, gs);
    PauseResult result = PAUSE_STAY;

    // Input: stylus on an item, or D-pad + A; B goes back, START resumes
    int down = keysDown();
    int chosen = -1;
    if (down & KEY_TOUCH) {
        touchPosition touch;
        touchRead(&touch);
        int row = (touch.py - ITEM_Y) / ITEM_PITCH;
        if (touch.py >= ITEM_Y && row < count && (touch.py - ITEM_Y) % ITEM_PITCH < ITEM_H &&
            touch.px >= BOX_X && touch.px < BOX_X + BOX_W) {
            cursor = row;
            chosen = row;
        }
    }
    if (down & KEY_DOWN) cursor = (cursor + 1) % count;
    if (down & KEY_UP) cursor = (cursor + count - 1) % count;
    if (down & KEY_A) chosen = cursor;
    if (chosen >= 0 && items[chosen].enabled) {
        result = activate(chosen, gs, terrain);
    } else if (chosen >= 0 && page != PAGE_LOAD) {
        say("No SD card to save to.");
    } else if (down & KEY_B) {
        if (page == PAGE_MAIN || loadOnly) { open = false; result = PAUSE_RESUME; }
        else go(PAGE_MAIN);
    } else if ((down & KEY_START) && !loadOnly) {
        open = false;
        result = PAUSE_RESUME;
    }
    if (result != PAUSE_STAY) return result;
    count = build_items(items, gs);   // the page or a label may have changed
    if (cursor >= count) cursor = 0;

    // Dim the frozen game, then the menu on top
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = y & 1; x < SCREEN_W; x += 2)
            buf[y * 256 + x] = PAL_BLACK;

    // A solid panel behind the menu, so nothing of the last page shows through
    for (int y = 2; y < ITEM_Y + MAX_ITEMS * ITEM_PITCH + 22; y++) {
        u8* row = buf + y * 256 + BOX_X - 8;
        for (int x = 0; x < BOX_W + 16; x++) row[x] = PAL_BLACK;
    }

    const char* title = TITLE[page];
    font_draw_str_8(buf, 256, SCREEN_H, (SCREEN_W - font_string_width(gameFont, title)) / 2, TITLE_Y,
                    title, PAL_YELLOW, gameFont);
    for (int i = 0; i < count; i++) {
        int y0 = ITEM_Y + i * ITEM_PITCH;
        for (int y = y0; y < y0 + ITEM_H; y++) {
            u8* row = buf + y * 256 + BOX_X;
            bool edge = (y == y0 || y == y0 + ITEM_H - 1);
            for (int x = 0; x < BOX_W; x++)
                row[x] = (edge || x == 0 || x == BOX_W - 1)
                         ? (i == cursor ? PAL_YELLOW : PAL_LIGHTBROWN) : PAL_DARKBROWN;
        }
        font_draw_str_8(buf, 256, SCREEN_H, BOX_X + 8, y0 + (ITEM_H - gameFont.height) / 2,
                        items[i].label, items[i].enabled ? PAL_WHITE : PAL_GRAY, gameFont);
    }
    if (messageTimer > 0) {
        messageTimer--;
        font_draw_str_8(buf, 256, SCREEN_H, (SCREEN_W - font_string_width(gameFont, message)) / 2,
                        ITEM_Y + MAX_ITEMS * ITEM_PITCH + 6, message, PAL_YELLOW, gameFont);
    }
    return PAUSE_STAY;
}
