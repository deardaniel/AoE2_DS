// AoE2 DSi - Villager on Grass (Tilemap + Sprite)
#include <nds.h>
#include <stdio.h>

#include <grass.h>
#include <dirt.h>
#include <villager.h>

typedef struct
{
    int x;
    int y;
    u16* sprite_gfx_mem;
    u8*  frame_gfx;
    int state;
    int anim_frame;
} Villager;

typedef struct
{
    int x;
    int y;
    bool active;
    u16* sprite_gfx_mem;
    int build_timer;
    int state; // 0 = foundation, 1 = built
} Building;

enum SpriteState {W_UP = 0, W_RIGHT = 1, W_DOWN = 2, W_LEFT = 3};
enum {SCREEN_TOP = 0, SCREEN_BOTTOM = 192, SCREEN_LEFT = 0, SCREEN_RIGHT = 256};
enum {MAP_W = 256, MAP_H = 256, SPRITE_SIZE = 32};

#define FRAMES_PER_ANIMATION 3
#define DIR_COUNT 4

void animateVillager(Villager *sprite)
{
    // Frames are packed in a 4x3 grid (columns = directions, rows = frames)
    int frame = sprite->state + sprite->anim_frame * DIR_COUNT;
    u8* offset = sprite->frame_gfx + frame * 32*32;
    dmaCopy(offset, sprite->sprite_gfx_mem, 32*32);
}

void initVillager(Villager *sprite, u8* gfx)
{
    sprite->sprite_gfx_mem = oamAllocateGfx(&oamMain, SpriteSize_32x32, SpriteColorFormat_256Color);
    sprite->frame_gfx = (u8*)gfx;
}

int main(void)
{
    Villager villager = {0,0};
    Building building = {0,0,false,0,0,0};
    int camX = 0;
    int camY = 0;
    bool followCam = true;
    
    // Main: sprites, Sub: bitmap map
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_5_2D);

    // VRAM: B for main sprites, C for sub BG bitmap
    vramSetBankA(VRAM_A_LCD);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    vramSetBankC(VRAM_C_SUB_BG);

    oamInit(&oamMain, SpriteMapping_1D_128, false);
    oamEnable(&oamMain);

    // BG2 bitmap (bottom screen)
    int bg2 = bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    u8* vram = (u8*)bgGetGfxPtr(bg2);
    dmaCopy(grassBitmap, vram, grassBitmapLen);
    dmaCopy(grassPal, BG_PALETTE_SUB, grassPalLen);
    bgShow(bg2);

    // Stamp a few dirt patches to make the terrain readable
    for (int patch = 0; patch < 3; patch++) {
        int px = 40 + patch * 120;
        int py = 60 + patch * 100;
        for (int y = 0; y < 96; y++) {
            u8* dst = vram + ((py + y) * 256) + px;
            u8* src = (u8*)dirtBitmap + ((y % 256) * 256);
            dmaCopy(src, dst, 96);
        }
    }
    
    initVillager(&villager, (u8*)villagerTiles);
    villager.x = 112;
    villager.y = 80;

    dmaCopy(villagerPal, SPRITE_PALETTE, 512);

    // Simple map marker (1px) for villager and building positions
    u8* map = (u8*)bgGetGfxPtr(bg2);

    static bool villagerSelected = false;

    while(pmMainLoop())
    {
        scanKeys();
        int keys = keysHeld();
        int keysPressed = keysDown();

        if(keys & KEY_START) break;
        if(keysPressed & KEY_SELECT) followCam = !followCam;

        touchPosition touch;
        touchRead(&touch);
        bool touchDown = (keys & KEY_TOUCH) != 0;
        bool touchPressed = (keysPressed & KEY_TOUCH) != 0;

        // Clear previous markers (very cheap: just overwrite last positions)
        static int lastVX = -1, lastVY = -1;
        static int lastBX = -1, lastBY = -1;
        if (lastVX >= 0 && lastVY >= 0) map[lastVY * 256 + lastVX] = 0;
        if (lastBX >= 0 && lastBY >= 0) map[lastBY * 256 + lastBX] = 0;

        // Selection/building/move logic via touch (bottom screen map)
        if (touchPressed) {
            int tx = touch.px + camX;
            int ty = touch.py + camY;

            bool onVillager = (tx >= villager.x && tx < villager.x + SPRITE_SIZE &&
                               ty >= villager.y && ty < villager.y + SPRITE_SIZE);

            if (onVillager) {
                villagerSelected = !villagerSelected;
            } else if (villagerSelected) {
                // Build at touch position
                building.active = true;
                building.x = tx - (SPRITE_SIZE / 2);
                building.y = ty - (SPRITE_SIZE / 2);
                if (building.x < 0) building.x = 0;
                if (building.y < 0) building.y = 0;
                if (building.x > (MAP_W - SPRITE_SIZE)) building.x = MAP_W - SPRITE_SIZE;
                if (building.y > (MAP_H - SPRITE_SIZE)) building.y = MAP_H - SPRITE_SIZE;
                building.build_timer = 90;
                building.state = 0;
            } else {
                // Move villager toward touch position
                villager.x = tx - (SPRITE_SIZE / 2);
                villager.y = ty - (SPRITE_SIZE / 2);
                if (villager.x < 0) villager.x = 0;
                if (villager.y < 0) villager.y = 0;
                if (villager.x > (MAP_W - SPRITE_SIZE)) villager.x = MAP_W - SPRITE_SIZE;
                if (villager.y > (MAP_H - SPRITE_SIZE)) villager.y = MAP_H - SPRITE_SIZE;
            }
        }

        // Simple idle animation tick
        if (touchDown) {
            villager.anim_frame++;
            if(villager.anim_frame >= FRAMES_PER_ANIMATION) villager.anim_frame = 0;
        }

        // Draw markers on the map
        int mvx = villager.x;
        int mvy = villager.y;
        if (mvx >= 0 && mvx < MAP_W && mvy >= 0 && mvy < MAP_H) {
            map[mvy * 256 + mvx] = villagerSelected ? 3 : 2;
            lastVX = mvx;
            lastVY = mvy;
        }

        if (building.active) {
            int mbx = building.x;
            int mby = building.y;
            if (mbx >= 0 && mbx < MAP_W && mby >= 0 && mby < MAP_H) {
                map[mby * 256 + mbx] = 1;
                lastBX = mbx;
                lastBY = mby;
            }
        }

        // Camera scroll (manual) with buttons, X/Y clamped to map bounds
        if(!followCam) {
            if(keys & KEY_L) { if(camY > 0) camY--; }
            if(keys & KEY_R) { if(camY < (MAP_H - 192)) camY++; }
            if(keys & KEY_A) { if(camX > 0) camX--; }
            if(keys & KEY_Y) { if(camX < (MAP_W - 256)) camX++; }
        }

        // Follow camera (center on villager)
        if(followCam) {
            camX = villager.x - (256 / 2);
            camY = villager.y - (192 / 2);
            if(camX < 0) camX = 0;
            if(camY < 0) camY = 0;
            if(camX > (MAP_W - 256)) camX = MAP_W - 256;
            if(camY > (MAP_H - 192)) camY = MAP_H - 192;
        }

        bgSetScroll(bg2, camX, camY);
        bgUpdate();

        animateVillager(&villager);

        int screenX = villager.x - camX;
        int screenY = villager.y - camY;

        oamSet(&oamMain, 0, screenX, screenY, 0, 0, SpriteSize_32x32, SpriteColorFormat_256Color,
            villager.sprite_gfx_mem, -1, false, false, false, villagerSelected, false);


        if (building.active) {
            if (building.build_timer > 0) {
                building.build_timer--;
                if (building.build_timer == 0) building.state = 1;
            }
            // Render building as a simple 32x32 solid tile from sprite memory (reuse villager gfx palette)
            if (!building.sprite_gfx_mem) {
                building.sprite_gfx_mem = oamAllocateGfx(&oamMain, SpriteSize_32x32, SpriteColorFormat_256Color);
            }

            // Update sprite gfx if state changes
            static int last_state = -1;
            if (building.state != last_state) {
                u8* dst = (u8*)building.sprite_gfx_mem;
                for (int y = 0; y < 32; y++) {
                    for (int x = 0; x < 32; x++) {
                        bool border = (x == 0 || y == 0 || x == 31 || y == 31);
                        if (building.state == 0) {
                            // Foundation: outline + sparse fill
                            if (border || ((x + y) % 6 == 0)) dst[y * 32 + x] = 1;
                            else dst[y * 32 + x] = 0;
                        } else {
                            // Built hut: border + fill + door
                            bool door = (y > 20 && y < 31 && x > 13 && x < 19);
                            if (border) dst[y * 32 + x] = 1;
                            else if (door) dst[y * 32 + x] = 3;
                            else dst[y * 32 + x] = 2;
                        }
                    }
                }
                last_state = building.state;
            }
            int bX = building.x - camX;
            int bY = building.y - camY;
            oamSet(&oamMain, 1, bX, bY, 0, 0, SpriteSize_32x32, SpriteColorFormat_256Color,
                building.sprite_gfx_mem, -1, false, false, false, false, false);
        }

        swiWaitForVBlank();
        oamUpdate(&oamMain);
    }

    return 0;
}
