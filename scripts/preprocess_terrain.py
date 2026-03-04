#!/usr/bin/env python3
"""Preprocess terrain tile textures into NDS-ready indexed binary data.

Extracts 32x16 isometric diamond terrain tiles from source images and indexes
them against the SPRITE palette (from sprite_pal.bin) so that terrain and
software-rendered sprites share the same BG palette at runtime.

Output:
  data/terrain_tiles.bin - 7 base terrain tiles + 4 grass variants = 11 tiles
                           Each tile is 32x16 pixels = 512 bytes
                           Layout: [0-6] = base terrain types, [7-10] = grass variants
                           Pixels outside the diamond mask are set to index 0
                           Inside pixels use sprite palette indices (16-255)

Requires: data/sprite_pal.bin must exist (run preprocess_sprites.py first)
"""

import os
import struct
import sys
import numpy as np
from PIL import Image

SPRITES_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'sprites')
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')
TERRAIN_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/terrain/textures'

ISO_TILE_W = 32
ISO_TILE_H = 16
NUM_TILES = 7  # TERRAIN_COUNT
GRASS_VARIANTS = 4  # Number of grass tile variants for visual variety

# Diamond mask: for each row, the start and end X of the filled region
# Must match ISO_DIAMOND_XSTART/XEND in iso.h
DIAMOND_XSTART = [15, 13, 11, 9, 7, 5, 3, 1, 1, 3, 5, 7, 9, 11, 13, 15]
DIAMOND_XEND   = [16, 18, 20, 22, 24, 26, 28, 30, 30, 28, 26, 24, 22, 20, 18, 16]

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
# Crop 32x16 regions for isometric tiles
TERRAIN_SOURCES = [
    # TERRAIN_GRASS (0): AoE2 HD grass texture (base variant — also used as fallback)
    (0, 'terrain', 'g_grs_00_color.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
    # TERRAIN_DIRT (1): AoE2 HD road/dirt texture
    (1, 'terrain', 'g_rd1_00_color.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
    # TERRAIN_WATER (2): AoE2 HD water texture
    (2, 'terrain', 'g_wtr_00_color.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
    # TERRAIN_FOREST (3): AoE2 HD forest texture
    (3, 'terrain', 'g_for_00_color.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
    # TERRAIN_GOLD (4): AoE2 HD desert texture (golden color)
    (4, 'terrain', 'g_des_00_color.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
    # TERRAIN_STONE (5): AoE2 HD rock texture
    (5, 'terrain', 'g_rck_00_COLOR.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
    # TERRAIN_FARM (6): AoE2 HD farm crop texture
    (6, 'terrain', 'g_fc1_00_color.png', (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H)),
]

# Grass variant sample positions — 4 well-spaced positions in the 512x512 texture
# chosen for maximum visual diversity (different brightness/color)
GRASS_VARIANT_CROPS = [
    (200, 200, 200 + ISO_TILE_W, 200 + ISO_TILE_H),  # variant 0: medium (base)
    (256, 224, 256 + ISO_TILE_W, 224 + ISO_TILE_H),  # variant 1: lighter
    (160, 448, 160 + ISO_TILE_W, 448 + ISO_TILE_H),  # variant 2: darker
    (128, 384, 128 + ISO_TILE_W, 384 + ISO_TILE_H),  # variant 3: greener
]


def load_tile(source_type, filename, crop):
    """Load a 32x16 tile crop from the source image."""
    if source_type == 'sprites':
        path = os.path.join(SPRITES_DIR, filename)
    else:
        path = os.path.join(TERRAIN_DIR, filename)

    if not os.path.exists(path):
        print(f"  WARNING: {path} not found")
        return None

    img = Image.open(path).convert('RGB')
    tile = img.crop(crop)

    # Ensure exactly 32x16
    if tile.size != (ISO_TILE_W, ISO_TILE_H):
        tile = tile.resize((ISO_TILE_W, ISO_TILE_H), Image.NEAREST)

    return np.array(tile)


def apply_diamond_mask(indexed_tile):
    """Set pixels outside the diamond shape to index 0 (transparent)."""
    for row in range(ISO_TILE_H):
        xs = DIAMOND_XSTART[row]
        xe = DIAMOND_XEND[row]
        for x in range(ISO_TILE_W):
            if x < xs or x >= xe:
                indexed_tile[row * ISO_TILE_W + x] = 0  # PAL_TRANSPARENT


def bgr555_to_rgb(val):
    """Convert NDS BGR555 to RGB888."""
    r = (val & 0x1F) << 3
    g = ((val >> 5) & 0x1F) << 3
    b = ((val >> 10) & 0x1F) << 3
    return (r, g, b)


def rgb_to_bgr555(r, g, b):
    """Convert RGB888 to NDS BGR555 format."""
    r5 = min(31, r >> 3)
    g5 = min(31, g >> 3)
    b5 = min(31, b >> 3)
    return (b5 << 10) | (g5 << 5) | r5 | (1 << 15)


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    print("=== NDS Terrain Tile Preprocessor (Isometric 32x16) ===\n")

    # Read sprite palette (generated by preprocess_sprites.py)
    sprite_pal_path = os.path.join(DATA_DIR, 'sprite_pal.bin')
    if not os.path.exists(sprite_pal_path):
        print(f"ERROR: {sprite_pal_path} not found. Run preprocess_sprites.py first.")
        sys.exit(1)

    print("Loading sprite palette...")
    with open(sprite_pal_path, 'rb') as f:
        sprite_pal_data = f.read()

    # Read remap table to find red player-color indices to exclude
    remap_path = os.path.join(DATA_DIR, 'sprite_remap.bin')
    excluded_indices = set()
    if os.path.exists(remap_path):
        with open(remap_path, 'rb') as f:
            remap = f.read()
        for i in range(256):
            if remap[i] != i:
                excluded_indices.add(remap[i])  # red target indices
        print(f"  Excluding {len(excluded_indices)} red player-color indices: {sorted(excluded_indices)}")

    # Extract RGB888 colors from sprite palette at indices 16-255
    # (indices 1-15 will be overwritten with UI colors at runtime)
    # Skip red player-color indices to prevent red bleeding in terrain
    sprite_colors = []
    valid_pal_indices = []  # actual palette index for each entry in sprite_colors
    for i in range(16, 256):
        if i in excluded_indices:
            continue
        val = struct.unpack_from('<H', sprite_pal_data, i * 2)[0]
        sprite_colors.append(bgr555_to_rgb(val))
        valid_pal_indices.append(i)
    print(f"  Loaded {len(sprite_colors)} sprite palette colors (indices 16-255, excl. red)")

    # Build palette array for nearest-color matching
    pal_array = np.array(sprite_colors, dtype=np.int32)

    # Load all terrain tiles
    print("\nLoading terrain tiles...")
    tiles = [None] * NUM_TILES

    for idx, source_type, filename, crop in TERRAIN_SOURCES:
        tile = load_tile(source_type, filename, crop)
        if tile is None:
            # Fallback: create a solid color tile
            print(f"  [{idx}] FALLBACK: solid color for {filename}")
            tile = np.full((ISO_TILE_H, ISO_TILE_W, 3), 128, dtype=np.uint8)
        tiles[idx] = tile
        print(f"  [{idx}] {filename}: {tile.shape[1]}x{tile.shape[0]}")

    # Index each tile against sprite palette and apply diamond mask
    print("\nIndexing terrain tiles against sprite palette (with diamond mask)...")
    all_tile_data = bytearray()

    for idx in range(NUM_TILES):
        tile = tiles[idx]
        h, w = tile.shape[:2]
        indexed = np.zeros(h * w, dtype=np.uint8)
        flat_rgb = tile.reshape(-1, 3).astype(np.int32)

        # Find nearest sprite palette color for each pixel
        for i in range(len(flat_rgb)):
            r, g, b = flat_rgb[i]
            diff = pal_array - np.array([r, g, b], dtype=np.int32)
            dists = np.sum(diff * diff, axis=1)
            nearest = np.argmin(dists)
            indexed[i] = valid_pal_indices[nearest]  # map back to actual palette index

        # Apply diamond mask: pixels outside diamond become transparent (0)
        apply_diamond_mask(indexed)

        all_tile_data.extend(bytes(indexed))
        print(f"  Tile {idx}: {w}x{h} = {w * h} bytes")

    # Generate grass variant tiles (appended after the 7 base terrain tiles)
    print(f"\nGenerating {GRASS_VARIANTS} grass variant tiles...")
    grass_src = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/terrain/textures/g_grs_00_color.png'
    if os.path.exists(grass_src):
        grass_img = Image.open(grass_src).convert('RGB')
        for vi, crop in enumerate(GRASS_VARIANT_CROPS):
            variant = np.array(grass_img.crop(crop))
            if variant.shape != (ISO_TILE_H, ISO_TILE_W, 3):
                variant = np.array(grass_img.crop(crop).resize((ISO_TILE_W, ISO_TILE_H), Image.NEAREST))

            flat_rgb = variant.reshape(-1, 3).astype(np.int32)
            indexed = np.zeros(ISO_TILE_W * ISO_TILE_H, dtype=np.uint8)
            for i in range(len(flat_rgb)):
                r, g, b = flat_rgb[i]
                diff = pal_array - np.array([r, g, b], dtype=np.int32)
                dists = np.sum(diff * diff, axis=1)
                nearest = np.argmin(dists)
                indexed[i] = valid_pal_indices[nearest]
            apply_diamond_mask(indexed)
            all_tile_data.extend(bytes(indexed))
            print(f"  Grass variant {vi}: {ISO_TILE_W}x{ISO_TILE_H} = {ISO_TILE_W * ISO_TILE_H} bytes")
    else:
        print(f"  WARNING: grass texture not found, duplicating base grass for variants")
        base_grass = all_tile_data[:ISO_TILE_W * ISO_TILE_H]
        for vi in range(GRASS_VARIANTS):
            all_tile_data.extend(base_grass)
            print(f"  Grass variant {vi}: fallback copy")

    tiles_path = os.path.join(DATA_DIR, 'terrain_tiles.bin')
    with open(tiles_path, 'wb') as f:
        f.write(all_tile_data)
    print(f"  {tiles_path}: {len(all_tile_data)} bytes")

    # Generate terrain_pal.bin with UI colors (for runtime overlay at indices 1-15)
    print("\nGenerating UI palette overlay...")
    pal_bin = bytearray(512)
    struct.pack_into('<H', pal_bin, 0, rgb_to_bgr555(0, 0, 0))
    for idx, (r, g, b) in UI_PALETTE.items():
        struct.pack_into('<H', pal_bin, idx * 2, rgb_to_bgr555(r, g, b))

    pal_path = os.path.join(DATA_DIR, 'terrain_pal.bin')
    with open(pal_path, 'wb') as f:
        f.write(pal_bin)
    print(f"  {pal_path}: {len(pal_bin)} bytes (UI colors at indices 1-15)")

    print(f"\nDone! Total: {len(pal_bin) + len(all_tile_data)} bytes")


if __name__ == '__main__':
    main()
