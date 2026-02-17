// AoE2 DSi - Villager on Grass (Fixed)
#include <nds.h>
#include <stdio.h>

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

#define FRAMES_PER_ANIMATION 3

void animateVillager(Villager *sprite)
{
    int frame = sprite->anim_frame + sprite->state * FRAMES_PER_ANIMATION;
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
    
    // Use MODE_0_2D for sprites (works!)
    videoSetMode(MODE_0_2D);
    videoSetModeSub(MODE_0_2D);

    vramSetBankA(VRAM_A_MAIN_SPRITE);
    vramSetBankD(VRAM_D_SUB_SPRITE);

    oamInit(&oamMain, SpriteMapping_1D_128, false);
    oamInit(&oamSub, SpriteMapping_1D_128, false);
    oamEnable(&oamMain);
    oamEnable(&oamSub);

    // Simple green background (just set BG0 to solid color)
    // This works in MODE_0_2D
    BG_PALETTE[0] = RGB15(0, 20, 0);  // Dark green grass
    
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
            if(keys & KEY_UP) { if(villager.y >= SCREEN_TOP) villager.y--; villager.state = W_UP; }
            if(keys & KEY_LEFT) { if(villager.x >= SCREEN_LEFT) villager.x--; villager.state = W_LEFT; }
            if(keys & KEY_RIGHT) { if(villager.x <= SCREEN_RIGHT) villager.x++; villager.state = W_RIGHT; }
            if(keys & KEY_DOWN) { if(villager.y <= SCREEN_BOTTOM) villager.y++; villager.state = W_DOWN; }

            villager.anim_frame++;
            if(villager.anim_frame >= FRAMES_PER_ANIMATION) villager.anim_frame = 0;
        }

        animateVillager(&villager);

        oamSet(&oamMain, 0, villager.x, villager.y, 0, 0, SpriteSize_32x32, SpriteColorFormat_256Color,
            villager.sprite_gfx_mem, -1, false, false, false, false, false);

        swiWaitForVBlank();
        oamUpdate(&oamMain);
        oamUpdate(&oamSub);
    }

    return 0;
}
