// AoE2 DSi - Sprite on Sub Screen (Bottom) Test
#include <nds.h>
#include <stdio.h>

int main(void) {
    // Main screen: bitmap (our grass background)
    videoSetMode(MODE_5_2D);
    
    // Sub screen: sprites + text
    videoSetModeSub(MODE_0_2D);
    
    // VRAM for main screen bitmap
    vramSetBankA(VRAM_A_MAIN_BG_0x06000000);
    
    // VRAM D for sub screen sprites
    vramSetBankD(VRAM_D_SUB_SPRITE);
    
    // Background on main screen (bitmap)
    int bg3 = bgInit(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
    u8* bgGfx = (u8*)bgGetGfxPtr(bg3);
    
    // Fill with grass green
    memset(bgGfx, 1, 256*256);
    BG_PALETTE[0] = RGB15(0,0,0);
    BG_PALETTE[1] = RGB15(0,20,0);
    
    // Sprites on SUB (bottom) screen
    oamInit(&oamSub, SpriteMapping_1D_128, false);
    oamEnable(&oamSub);
    
    // Allocate 32x32 sprite
    u16* spriteMem = oamAllocateGfx(&oamSub, SpriteSize_32x32, SpriteColorFormat_256Color);
    
    // Fill with color index 1 (will be red)
    memset(spriteMem, 1, 32*32);
    
    // Set palette - index 1 = RED
    SPRITE_PALETTE_SUB[1] = RGB15(31, 0, 0);
    
    // Draw sprite on bottom screen
    oamSet(&oamSub, 0, 112, 80, 0, 0, SpriteSize_32x32, 
            SpriteColorFormat_256Color, spriteMem, -1, false, false, false, false, false);
    
    // Console on bottom
    consoleDemoInit();
    iprintf("Bitmap top, Sprite\n");
    iprintf("on BOTTOM screen\n");
    iprintf("Red square should\n");
    iprintf("be on bottom\n");
    
    while(pmMainLoop()) {
        swiWaitForVBlank();
        oamUpdate(&oamSub);
    }
    
    return 0;
}
