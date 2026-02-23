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
} Building;

enum SpriteState {W_UP = 0, W_RIGHT = 1, W_DOWN = 2, W_LEFT = 3};
enum {SCREEN_TOP = 0, SCREEN_BOTTOM = 192, SCREEN_LEFT = 0, SCREEN_RIGHT = 256};
enum {MAP_W = 512, MAP_H = 512, SPRITE_SIZE = 32};

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
    Building building = {0,0,false,0};
    int camX = 0;
    int camY = 0;
    bool followCam = true;
    
    // Use MODE_5_2D for bitmap BG + sprites
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_0_2D);

    // VRAM: A for main BG bitmap, B for main sprites
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankB(VRAM_B_MAIN_SPRITE);
    vramSetBankD(VRAM_D_SUB_SPRITE);

    oamInit(&oamMain, SpriteMapping_1D_128, false);
    oamInit(&oamSub, SpriteMapping_1D_128, false);
    oamEnable(&oamMain);
    oamEnable(&oamSub);

    // BG2 bitmap (top screen)
    int bg2 = bgInit(2, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    dmaCopy(grassPal, BG_PALETTE, grassPalLen);
    bgShow(bg2);

    // Initialize a simple larger map (2x2 screens) by tiling the 256x256 grass bitmap
    u8* vram = (u8*)bgGetGfxPtr(bg2);
    for (int ty = 0; ty < 2; ty++) {
        for (int tx = 0; tx < 2; tx++) {
            int baseX = tx * 256;
            int baseY = ty * 256;
            for (int y = 0; y < 256; y++) {
                u8* dst = vram + ((baseY + y) * 512) + baseX;
                u8* src = (u8*)grassBitmap + (y * 256);
                dmaCopy(src, dst, 256);
            }
        }
    }

    // Stamp a few dirt patches to make the terrain readable
    for (int patch = 0; patch < 3; patch++) {
        int px = 40 + patch * 120;
        int py = 60 + patch * 100;
        for (int y = 0; y < 96; y++) {
            u8* dst = vram + ((py + y) * 512) + px;
            u8* src = (u8*)dirtBitmap + ((y % 256) * 256);
            dmaCopy(src, dst, 96);
        }
    }
    
    initVillager(&villager, (u8*)villagerTiles);
    villager.x = 112;
    villager.y = 80;

    dmaCopy(villagerPal, SPRITE_PALETTE, 512);

    consoleDemoInit();
    iprintf("AoE2 DSi - Grass\n");
    iprintf("Villager: %d,%d\n", villager.x, villager.y);
    iprintf("D-pad: move\n");
    iprintf("SELECT: toggle camera follow\n");
    iprintf("A: select villager\n");
    iprintf("B: build here\n");
    iprintf("START: exit\n");

    static bool villagerSelected = false;

    while(pmMainLoop())
    {
        scanKeys();
        int keys = keysHeld();

        if(keys & KEY_START) break;
        if(keys & KEY_SELECT) followCam = !followCam;
        bool selectVillager = (keysDown() & KEY_A) != 0;
        bool buildHere = (keysDown() & KEY_B) != 0;

        // Selection/building logic
        if (selectVillager) villagerSelected = !villagerSelected;

        if(keys && !villagerSelected)
        {
            if(keys & KEY_UP) { if(villager.y > 0) villager.y--; villager.state = W_UP; }
            if(keys & KEY_LEFT) { if(villager.x > 0) villager.x--; villager.state = W_LEFT; }
            if(keys & KEY_RIGHT) { if(villager.x < (MAP_W - SPRITE_SIZE)) villager.x++; villager.state = W_RIGHT; }
            if(keys & KEY_DOWN) { if(villager.y < (MAP_H - SPRITE_SIZE)) villager.y++; villager.state = W_DOWN; }

            villager.anim_frame++;
            if(villager.anim_frame >= FRAMES_PER_ANIMATION) villager.anim_frame = 0;
        }

        // Camera scroll (manual) with L/R, X/Y clamped to map bounds
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

        if (buildHere && villagerSelected) {
            building.active = true;
            building.x = villager.x;
            building.y = villager.y;
        }

        if (building.active) {
            // Render building as a simple 32x32 solid tile from sprite memory (reuse villager gfx palette)
            if (!building.sprite_gfx_mem) {
                building.sprite_gfx_mem = oamAllocateGfx(&oamMain, SpriteSize_32x32, SpriteColorFormat_256Color);
                // Simple hut block: border + fill using existing palette indices
                u8* dst = (u8*)building.sprite_gfx_mem;
                for (int y = 0; y < 32; y++) {
                    for (int x = 0; x < 32; x++) {
                        bool border = (x == 0 || y == 0 || x == 31 || y == 31);
                        bool door = (y > 20 && y < 31 && x > 13 && x < 19);
                        if (border) dst[y * 32 + x] = 1;
                        else if (door) dst[y * 32 + x] = 3;
                        else dst[y * 32 + x] = 2;
                    }
                }
            }
            int bX = building.x - camX;
            int bY = building.y - camY;
            oamSet(&oamMain, 1, bX, bY, 0, 0, SpriteSize_32x32, SpriteColorFormat_256Color,
                building.sprite_gfx_mem, -1, false, false, false, false, false);
        }

        swiWaitForVBlank();
        oamUpdate(&oamMain);
        oamUpdate(&oamSub);
    }

    return 0;
}
