#!/usr/bin/env python3
"""
Generate font_aoe2.bin — a 1bpp bitmap font for the AoE2 DSi project.

Binary format:
  [0] height (u8)
  [1] num_chars (u8)
  [2] start_char (u8) — ASCII code of first character
  [3] padding
  [4..4+num_chars-1] width table (u8 per char)
  [4+num_chars..] bitmap data: 1bpp per char, row-major
      Each row: ceil(width/8) bytes, MSB = leftmost pixel

Uses DejaVu Serif Bold as a stand-in for Century Bold — a heavy serif
face that reads well at small sizes on NDS.
"""

import os
import struct
from PIL import Image, ImageDraw, ImageFont

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DATA_DIR = os.path.join(PROJECT_DIR, "data")

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf"
FONT_SIZE = 11  # pixels — produces ~14px height with ascenders/descenders
START_CHAR = 32  # space
NUM_CHARS = 96   # 32..127 (full printable ASCII)
TARGET_HEIGHT = 14


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)

    # First pass: determine max height and per-character metrics
    # Render each character, find bounding box
    char_images = []
    widths = []

    for i in range(NUM_CHARS):
        ch = chr(START_CHAR + i)
        # Render character to get exact bounds
        # Use a temp image large enough
        tmp = Image.new("L", (32, 32), 0)
        draw = ImageDraw.Draw(tmp)
        draw.text((0, 0), ch, fill=255, font=font)

        # Find actual bounding box of rendered pixels
        bbox = tmp.getbbox()
        if bbox is None:
            # Space or empty character
            # Use a reasonable width based on font metrics
            left, top, right, bottom = font.getbbox(ch)
            w = max(right - left, 3)
            char_images.append(None)
            widths.append(w)
        else:
            left, top, right, bottom = bbox
            w = right  # width from x=0
            char_images.append(tmp)
            widths.append(w)

    # Second pass: render all characters at consistent baseline into TARGET_HEIGHT
    # Find the font's ascent to align baselines
    ascent, descent = font.getmetrics()

    # We'll render into TARGET_HEIGHT images, positioning text so baseline is consistent
    # Top padding to vertically center the font in TARGET_HEIGHT
    y_offset = max(0, (TARGET_HEIGHT - (ascent + descent)) // 2)

    final_images = []
    final_widths = []

    for i in range(NUM_CHARS):
        ch = chr(START_CHAR + i)
        # Render into a properly sized image
        tmp = Image.new("L", (32, TARGET_HEIGHT), 0)
        draw = ImageDraw.Draw(tmp)
        draw.text((0, y_offset), ch, fill=255, font=font)

        # Find actual width used
        bbox = tmp.getbbox()
        if bbox is None:
            # Space character
            left, top, right, bottom = font.getbbox(ch)
            w = max(right - left, 3)
            final_widths.append(w)
            final_images.append(Image.new("L", (w, TARGET_HEIGHT), 0))
        else:
            w = bbox[2]  # right edge
            if w < 1:
                w = 3
            final_widths.append(w)
            final_images.append(tmp.crop((0, 0, w, TARGET_HEIGHT)))

    # Build binary
    out = bytearray()
    out.append(TARGET_HEIGHT)       # [0] height
    out.append(NUM_CHARS)           # [1] num_chars
    out.append(START_CHAR)          # [2] start_char
    out.append(0)                   # [3] padding

    # Width table
    for w in final_widths:
        out.append(min(w, 255))

    # Bitmap data: 1bpp, row-major, MSB = leftmost
    for i in range(NUM_CHARS):
        img = final_images[i]
        w = final_widths[i]
        bytesPerRow = (w + 7) // 8
        for row in range(TARGET_HEIGHT):
            row_bytes = [0] * bytesPerRow
            for px in range(w):
                if px < img.width and row < img.height:
                    pixel = img.getpixel((px, row))
                    if pixel > 127:  # threshold
                        byteIdx = px // 8
                        bitIdx = 7 - (px % 8)
                        row_bytes[byteIdx] |= (1 << bitIdx)
            out.extend(row_bytes)

    out_path = os.path.join(DATA_DIR, "font_aoe2.bin")
    with open(out_path, "wb") as f:
        f.write(bytes(out))
    print(f"Wrote {out_path} ({len(out)} bytes)")
    print(f"  Height: {TARGET_HEIGHT}, Chars: {NUM_CHARS}, Start: {START_CHAR}")

    # Verify a few characters
    print("\nSample glyphs:")
    for ch in "AaBbMm":
        idx = ord(ch) - START_CHAR
        w = final_widths[idx]
        print(f"  '{ch}' width={w}")


if __name__ == "__main__":
    main()
