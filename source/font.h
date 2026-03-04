#pragma once
#include <nds.h>

// ---------------------------------------------------------------------------
// AoE2 bitmap font renderer
// Binary format (font_aoe2.bin):
//   [0] height (u8)
//   [1] num_chars (u8)
//   [2] start_char (u8) — ASCII code of first character
//   [3] padding
//   [4..4+num_chars-1] width table (u8 per char)
//   [4+num_chars..] bitmap data: 1bpp per char, row-major
//       Each row: ceil(width/8) bytes, MSB = leftmost pixel
// ---------------------------------------------------------------------------

extern const u8 font_aoe2_bin[];

struct BitmapFont {
    u8 height;
    u8 numChars;
    u8 startChar;
    const u8* widths;
    const u8* bitmaps;
    // Precomputed offset table for fast glyph lookup
    const u8* glyphStart[128]; // pointer to start of each glyph's bitmap
};

// Global font instance (initialized in font_init)
extern BitmapFont gameFont;

// Initialize the font from binary data — call once at startup
void font_init();

// Get width of a character
static inline int font_char_width(const BitmapFont& f, char ch) {
    int idx = (int)ch - f.startChar;
    if (idx < 0 || idx >= f.numChars) return 4; // space fallback
    return f.widths[idx];
}

// Get width of a string in pixels
static inline int font_string_width(const BitmapFont& f, const char* str) {
    int w = 0;
    while (*str) {
        w += font_char_width(f, *str) + 1; // +1 for spacing
        str++;
    }
    if (w > 0) w--; // remove trailing space
    return w;
}

// ---------------------------------------------------------------------------
// Draw a character into a 16-bit (RGB15) VRAM buffer (top screen)
// Returns X position after the character
// ---------------------------------------------------------------------------
static inline int font_draw_char_16(u16* vram, int screenW, int screenH,
                                     int x, int y, char ch, u16 color,
                                     const BitmapFont& f) {
    int idx = (int)ch - f.startChar;
    if (idx < 0 || idx >= f.numChars) return x + 4;

    int w = f.widths[idx];
    const u8* data = f.glyphStart[idx];

    for (int row = 0; row < f.height; row++) {
        int sy = y + row;
        if (sy < 0 || sy >= screenH) {
            data += (w + 7) / 8;
            continue;
        }
        int bytesPerRow = (w + 7) / 8;
        for (int px = 0; px < w; px++) {
            int sx = x + px;
            if (sx < 0 || sx >= screenW) continue;
            int byteIdx = px / 8;
            int bitIdx = 7 - (px % 8);
            if (data[byteIdx] & (1 << bitIdx)) {
                vram[sy * screenW + sx] = color;
            }
        }
        data += bytesPerRow;
    }
    return x + w;
}

// Draw a string into a 16-bit buffer
static inline int font_draw_str_16(u16* vram, int screenW, int screenH,
                                    int x, int y, const char* str, u16 color,
                                    const BitmapFont& f) {
    while (*str) {
        x = font_draw_char_16(vram, screenW, screenH, x, y, *str, color, f);
        x += 1; // inter-character spacing
        str++;
    }
    return x;
}

// Draw a number into a 16-bit buffer
static inline int font_draw_num_16(u16* vram, int screenW, int screenH,
                                    int x, int y, int val, u16 color,
                                    const BitmapFont& f) {
    if (val < 0) val = 0;
    char buf[12];
    int n = 0;
    if (val == 0) { buf[n++] = '0'; }
    else { while (val > 0 && n < 10) { buf[n++] = '0' + (val % 10); val /= 10; } }
    // Reverse
    for (int i = 0; i < n / 2; i++) {
        char tmp = buf[i]; buf[i] = buf[n-1-i]; buf[n-1-i] = tmp;
    }
    buf[n] = '\0';
    return font_draw_str_16(vram, screenW, screenH, x, y, buf, color, f);
}

// ---------------------------------------------------------------------------
// Draw a character into an 8-bit (paletted) bitmap buffer (bottom screen)
// Returns X position after the character
// ---------------------------------------------------------------------------
static inline int font_draw_char_8(u8* buf, int screenW, int screenH,
                                    int x, int y, char ch, u8 palIdx,
                                    const BitmapFont& f) {
    int idx = (int)ch - f.startChar;
    if (idx < 0 || idx >= f.numChars) return x + 4;

    int w = f.widths[idx];
    const u8* data = f.glyphStart[idx];

    for (int row = 0; row < f.height; row++) {
        int sy = y + row;
        if (sy < 0 || sy >= screenH) {
            data += (w + 7) / 8;
            continue;
        }
        int bytesPerRow = (w + 7) / 8;
        for (int px = 0; px < w; px++) {
            int sx = x + px;
            if (sx < 0 || sx >= screenW) continue;
            int byteIdx = px / 8;
            int bitIdx = 7 - (px % 8);
            if (data[byteIdx] & (1 << bitIdx)) {
                buf[sy * screenW + sx] = palIdx;
            }
        }
        data += bytesPerRow;
    }
    return x + w;
}

// Draw a string into an 8-bit buffer
static inline int font_draw_str_8(u8* buf, int screenW, int screenH,
                                   int x, int y, const char* str, u8 palIdx,
                                   const BitmapFont& f) {
    while (*str) {
        x = font_draw_char_8(buf, screenW, screenH, x, y, *str, palIdx, f);
        x += 1;
        str++;
    }
    return x;
}

// Draw a number into an 8-bit buffer
static inline int font_draw_num_8(u8* buf, int screenW, int screenH,
                                   int x, int y, int val, u8 palIdx,
                                   const BitmapFont& f) {
    if (val < 0) val = 0;
    char buf2[12];
    int n = 0;
    if (val == 0) { buf2[n++] = '0'; }
    else { while (val > 0 && n < 10) { buf2[n++] = '0' + (val % 10); val /= 10; } }
    for (int i = 0; i < n / 2; i++) {
        char tmp = buf2[i]; buf2[i] = buf2[n-1-i]; buf2[n-1-i] = tmp;
    }
    buf2[n] = '\0';
    return font_draw_str_8(buf, screenW, screenH, x, y, buf2, palIdx, f);
}
