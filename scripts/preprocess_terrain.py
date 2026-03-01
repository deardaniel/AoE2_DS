#!/usr/bin/env python3
"""Preprocess terrain tile textures into NDS-ready indexed binary data.

Extracts 16x16 terrain tiles from source images and builds a shared 256-color
BG palette. Handles 7 terrain types: grass, dirt, water, forest, gold, stone, farm.

Output:
  data/terrain_pal.bin   - 256-entry BGR555 palette (512 bytes)
  data/terrain_tiles.bin - 7 terrain tiles × 16×16 pixels = 1792 bytes

Palette layout:
  Index 0:     Transparent / unused
  Index 1-15:  Reserved for UI colors (PAL_BLACK through PAL_DARKBROWN)
  Index 16-255: Terrain tile colors (shared across all 7 tiles)
"""

import os
import struct
import sys
import numpy as np
from PIL import Image

SPRITES_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'sprites')
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')
TERRAIN_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/terrain/textures'

TILE_PX = 16
NUM_TILES = 7  # TERRAIN_COUNT

# UI palette entries (indices 1-15) — must match config.h PAL_* values
UI_PALETTE = {
    1:  (0,   0,   0),    # PAL_BLACK
    2:  (80,  48,  16),   # PAL_BROWN
    3:  (160, 128, 80),   # PAL_TAN
    4:  (32,  64,  224),  # PAL_BLUE
    5:  (224, 32,  32),   # PAL_RED
    6:  (16,  96,  16),   # PAL_DARKGREEN
    7:  (32,  160, 32),   # PAL_GREEN
    8:  (224, 224, 32),   # PAL_YELLOW
    9:  (128, 128, 128),  # PAL_GRAY
    10: (64,  64,  64),   # PAL_DARKGRAY
    11: (248, 248, 248),  # PAL_WHITE
    12: (224, 128, 32),   # PAL_ORANGE
    13: (176, 144, 80),   # PAL_LIGHTBROWN
    14: (224, 176, 128),  # PAL_SKIN
    15: (48,  32,  16),   # PAL_DARKBROWN
}

# Terrain tile sources: (type_index, source_path_or_sprite, crop_region)
# crop_region = (left, top, right, bottom) in the source image
TERRAIN_SOURCES = [
    # TERRAIN_GRASS (0): existing grass.png from sprites/
    (0, 'sprites', 'grass.png', (0, 0, TILE_PX, TILE_PX)),
    # TERRAIN_DIRT (1): existing dirt.png from sprites/
    (1, 'sprites', 'dirt.png', (0, 0, TILE_PX, TILE_PX)),
    # TERRAIN_WATER (2): existing water.png from sprites/
    (2, 'sprites', 'water.png', (0, 0, TILE_PX, TILE_PX)),
    # TERRAIN_FOREST (3): AoE2 HD forest texture
    (3, 'terrain', 'g_for_00_color.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
    # TERRAIN_GOLD (4): AoE2 HD desert texture (golden color)
    (4, 'terrain', 'g_des_00_color.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
    # TERRAIN_STONE (5): AoE2 HD rock texture
    (5, 'terrain', 'g_rck_00_COLOR.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
    # TERRAIN_FARM (6): AoE2 HD farm texture
    (6, 'terrain', 'g_fm1_00_color.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
]


def load_tile(source_type, filename, crop):
    """Load a 16x16 tile crop from the source image."""
    if source_type == 'sprites':
        path = os.path.join(SPRITES_DIR, filename)
    else:
        path = os.path.join(TERRAIN_DIR, filename)

    if not os.path.exists(path):
        print(f"  WARNING: {path} not found")
        return None

    img = Image.open(path).convert('RGB')
    tile = img.crop(crop)

    # Ensure exactly 16x16
    if tile.size != (TILE_PX, TILE_PX):
        tile = tile.resize((TILE_PX, TILE_PX), Image.NEAREST)

    return np.array(tile)


def rgb_to_bgr555(r, g, b):
    """Convert RGB888 to NDS BGR555 format."""
    r5 = min(31, r >> 3)
    g5 = min(31, g >> 3)
    b5 = min(31, b >> 3)
    return (b5 << 10) | (g5 << 5) | r5 | (1 << 15)


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    print("=== NDS Terrain Tile Preprocessor ===\n")

    # Load all terrain tiles
    print("Loading terrain tiles...")
    tiles = [None] * NUM_TILES
    all_pixels = []

    for idx, source_type, filename, crop in TERRAIN_SOURCES:
        tile = load_tile(source_type, filename, crop)
        if tile is None:
            # Fallback: create a solid color tile
            print(f"  [{idx}] FALLBACK: solid color for {filename}")
            tile = np.full((TILE_PX, TILE_PX, 3), 128, dtype=np.uint8)
        tiles[idx] = tile
        all_pixels.append(tile.reshape(-1, 3))
        print(f"  [{idx}] {filename}: {tile.shape[1]}x{tile.shape[0]}")

    # Build shared terrain palette
    print("\nBuilding shared palette...")
    all_px = np.vstack(all_pixels)
    unique_colors = np.unique(all_px, axis=0)
    print(f"  {len(unique_colors)} unique colors across all tiles")

    # We have 240 slots (indices 16-255). Quantize to fit.
    max_terrain_colors = 240

    if len(unique_colors) <= max_terrain_colors:
        palette = [tuple(c) for c in unique_colors]
    else:
        # Quantize using PIL
        n = len(unique_colors)
        w = min(n, 256)
        h = (n + w - 1) // w
        composite = Image.new('RGB', (w, h), (0, 0, 0))
        pixels = composite.load()
        for i, (r, g, b) in enumerate(unique_colors):
            pixels[i % w, i // w] = (int(r), int(g), int(b))
        quantized = composite.quantize(colors=max_terrain_colors, method=Image.Quantize.MEDIANCUT)
        pal_flat = quantized.getpalette()
        palette = []
        for i in range(max_terrain_colors):
            palette.append((pal_flat[i * 3], pal_flat[i * 3 + 1], pal_flat[i * 3 + 2]))
        # Deduplicate
        seen = set()
        deduped = []
        for c in palette:
            if c not in seen:
                seen.add(c)
                deduped.append(c)
        palette = deduped

    print(f"  Terrain palette: {len(palette)} colors (indices 16-{16 + len(palette) - 1})")

    # Pad to 240
    while len(palette) < max_terrain_colors:
        palette.append((0, 0, 0))

    # Build full 256-entry NDS palette
    print("\nBuilding NDS palette...")
    pal_bin = bytearray(512)

    # Index 0: transparent/black
    struct.pack_into('<H', pal_bin, 0, rgb_to_bgr555(0, 0, 0))

    # Indices 1-15: UI colors
    for idx, (r, g, b) in UI_PALETTE.items():
        struct.pack_into('<H', pal_bin, idx * 2, rgb_to_bgr555(r, g, b))

    # Indices 16-255: terrain colors
    for i, (r, g, b) in enumerate(palette):
        struct.pack_into('<H', pal_bin, (16 + i) * 2, rgb_to_bgr555(r, g, b))

    pal_path = os.path.join(DATA_DIR, 'terrain_pal.bin')
    with open(pal_path, 'wb') as f:
        f.write(pal_bin)
    print(f"  {pal_path}: {len(pal_bin)} bytes")

    # Build palette lookup array (for indexing tiles)
    pal_array = np.array(palette, dtype=np.int32)

    # Index each tile
    print("\nIndexing terrain tiles...")
    all_tile_data = bytearray()

    for idx in range(NUM_TILES):
        tile = tiles[idx]
        h, w = tile.shape[:2]
        indexed = np.zeros(h * w, dtype=np.uint8)
        flat_rgb = tile.reshape(-1, 3).astype(np.int32)

        # Find nearest palette color for each pixel
        for i in range(len(flat_rgb)):
            r, g, b = flat_rgb[i]
            diff = pal_array - np.array([r, g, b], dtype=np.int32)
            dists = np.sum(diff * diff, axis=1)
            nearest = np.argmin(dists)
            indexed[i] = nearest + 16  # offset by 16 for UI palette reservation

        all_tile_data.extend(bytes(indexed))
        print(f"  Tile {idx}: {w}x{h} = {w * h} bytes")

    tiles_path = os.path.join(DATA_DIR, 'terrain_tiles.bin')
    with open(tiles_path, 'wb') as f:
        f.write(all_tile_data)
    print(f"  {tiles_path}: {len(all_tile_data)} bytes")

    print(f"\nDone! Total: {len(pal_bin) + len(all_tile_data)} bytes")


if __name__ == '__main__':
    main()
