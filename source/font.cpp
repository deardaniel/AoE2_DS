#include "font.h"

extern const u8 font_aoe2_bin[];

BitmapFont gameFont;

void font_init() {
    gameFont.height = font_aoe2_bin[0];
    gameFont.numChars = font_aoe2_bin[1];
    gameFont.startChar = font_aoe2_bin[2];
    gameFont.widths = &font_aoe2_bin[4];

    // Compute glyph start pointers
    const u8* bitmapData = &font_aoe2_bin[4 + gameFont.numChars];
    for (int i = 0; i < gameFont.numChars && i < 128; i++) {
        gameFont.glyphStart[i] = bitmapData;
        int w = gameFont.widths[i];
        int bytesPerRow = (w + 7) / 8;
        bitmapData += bytesPerRow * gameFont.height;
    }
}
