#!/usr/bin/env python3
"""
Generate panel textures for the top screen UI.

Outputs:
  data/tex_wood_16.bin      - 16x16 dark wood tile (512 bytes, RGB15)
  data/tex_parchment_64.bin - 64x64 parchment tile (8192 bytes, RGB15)
"""

import os
import random
import struct
from PIL import Image

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
DATA_DIR = os.path.join(PROJECT_DIR, "data")
SLP_DIR = "/tmp/slp_51119"


def to_rgb15(r, g, b):
    """Convert 8-bit RGB to NDS RGB15 with alpha bit set."""
    r5 = (r >> 3) & 0x1F
    g5 = (g >> 3) & 0x1F
    b5 = (b >> 3) & 0x1F
    return r5 | (g5 << 5) | (b5 << 10) | (1 << 15)


def extract_tile(img, tile_size):
    """Resize image to tile_size x tile_size and convert to RGB15 binary."""
    tile = img.resize((tile_size, tile_size), Image.LANCZOS).convert("RGB")
    data = bytearray()
    for y in range(tile_size):
        for x in range(tile_size):
            r, g, b = tile.getpixel((x, y))
            data += struct.pack("<H", to_rgb15(r, g, b))
    return bytes(data)


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    # Dark wood tile — use the pre-extracted 64x64 wood tile, downscaled to 16x16
    wood_path = os.path.join(SLP_DIR, "wood_tile_64.png")
    if not os.path.exists(wood_path):
        frame = Image.open(os.path.join(SLP_DIR, "frame_0.png"))
        wood_img = frame.crop((30, 510, 30 + 64, 510 + 64))
    else:
        wood_img = Image.open(wood_path)

    wood_bin = extract_tile(wood_img, 16)
    out_wood = os.path.join(DATA_DIR, "tex_wood_16.bin")
    with open(out_wood, "wb") as f:
        f.write(wood_bin)
    print(f"Wrote {out_wood} ({len(wood_bin)} bytes)")

    # Parchment tile — procedural 64x64 with subtle noise
    # Base color sampled from SLP 51119 parchment area: RGB(205, 178, 145)
    random.seed(42)
    size = 64
    base_r, base_g, base_b = 205, 178, 145
    data = bytearray()
    for y in range(size):
        for x in range(size):
            nr = max(0, min(255, base_r + random.randint(-8, 8)))
            ng = max(0, min(255, base_g + random.randint(-8, 8)))
            nb = max(0, min(255, base_b + random.randint(-8, 8)))
            data += struct.pack("<H", to_rgb15(nr, ng, nb))

    out_parch = os.path.join(DATA_DIR, "tex_parchment_64.bin")
    with open(out_parch, "wb") as f:
        f.write(bytes(data))
    print(f"Wrote {out_parch} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
