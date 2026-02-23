// AoE2 DSi - Villager on Grass (Tilemap + Sprite)
#include <nds.h>
#include <stdio.h>

#include <grass.h>
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
    int camX = 0;
    int camY = 0;
    
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
    dmaCopy(grassBitmap, bgGetGfxPtr(bg2), grassBitmapLen);
    dmaCopy(grassPal, BG_PALETTE, grassPalLen);
    bgShow(bg2);
    
    initVillager(&villager, (u8*)villagerTiles);
    villager.x = 112;
    villager.y = 80;

    dmaCopy(villagerPal, SPRITE_PALETTE, 512);

    consoleDemoInit();
    iprintf("AoE2 DSi - Grass\n");
    iprintf("Villager: %d,%d\n", villager.x, villager.y);
    iprintf("D-pad: move\n");
    iprintf("START: exit\n");

    while(pmMainLoop())
    {
        scanKeys();
        int keys = keysHeld();

        if(keys & KEY_START) break;

        if(keys)
        {
            if(keys & KEY_UP) { if(villager.y > 0) villager.y--; villager.state = W_UP; }
            if(keys & KEY_LEFT) { if(villager.x > 0) villager.x--; villager.state = W_LEFT; }
            if(keys & KEY_RIGHT) { if(villager.x < (MAP_W - SPRITE_SIZE)) villager.x++; villager.state = W_RIGHT; }
            if(keys & KEY_DOWN) { if(villager.y < (MAP_H - SPRITE_SIZE)) villager.y++; villager.state = W_DOWN; }

            villager.anim_frame++;
            if(villager.anim_frame >= FRAMES_PER_ANIMATION) villager.anim_frame = 0;
        }

        // Camera scroll (manual) with L/R, X/Y clamped to map bounds
        if(keys & KEY_L) { if(camY > 0) camY--; }
        if(keys & KEY_R) { if(camY < (MAP_H - 192)) camY++; }
        if(keys & KEY_A) { if(camX > 0) camX--; }
        if(keys & KEY_Y) { if(camX < (MAP_W - 256)) camX++; }

        bgSetScroll(bg2, camX, camY);
        bgUpdate();

        animateVillager(&villager);

        int screenX = villager.x - camX;
        int screenY = villager.y - camY;

        oamSet(&oamMain, 0, screenX, screenY, 0, 0, SpriteSize_32x32, SpriteColorFormat_256Color,
            villager.sprite_gfx_mem, -1, false, false, false, false, false);

        swiWaitForVBlank();
        oamUpdate(&oamMain);
        oamUpdate(&oamSub);
    }

    return 0;
}
