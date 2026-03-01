#!/usr/bin/env python3
"""Combined preprocessor: builds a single shared palette for terrain + sprites.

Generates all binary data files for the NDS game with one unified palette
so that software-rendered sprites and bitmap terrain share the same colors.

Palette layout (256 entries):
  Index 0:       Transparent / black
  Index 1-15:    UI colors (PAL_BLACK through PAL_DARKBROWN)
  Index 16-255:  Shared terrain + sprite colors (240 entries)

Output files:
  data/terrain_pal.bin     - Shared 256-entry BGR555 palette (512 bytes)
  data/terrain_tiles.bin   - 7 terrain tiles x 16x16 pixels (1792 bytes)
  data/sprite_pal.bin      - Same shared palette (for SPRITE_PALETTE_SUB)
  data/sprite_remap.bin    - Player 2 color remap table (256 bytes)
  data/spr_*.bin           - Indexed sprite data files
"""

import os
import struct
import sys
import numpy as np
from PIL import Image

BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SPRITES_DIR = os.path.join(BASE_DIR, 'sprites')
DATA_DIR = os.path.join(BASE_DIR, 'data')
TERRAIN_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/terrain/textures'

TILE_PX = 16
NUM_TILES = 7

# UI palette entries (indices 1-15)
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

# Terrain tile sources
TERRAIN_SOURCES = [
    (0, 'sprites', 'grass.png', (0, 0, TILE_PX, TILE_PX)),
    (1, 'sprites', 'dirt.png', (0, 0, TILE_PX, TILE_PX)),
    (2, 'sprites', 'water.png', (0, 0, TILE_PX, TILE_PX)),
    (3, 'terrain', 'g_for_00_color.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
    (4, 'terrain', 'g_des_00_color.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
    (5, 'terrain', 'g_rck_00_COLOR.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
    (6, 'terrain', 'g_fm1_00_color.png', (200, 200, 200 + TILE_PX, 200 + TILE_PX)),
]

# Unit sprite sheets
UNIT_SHEETS = [
    ('spr_villager',       'villager.png'),
    ('spr_villager_walk',  'villager_walk.png'),
    ('spr_villager_f',     'villager_f.png'),
    ('spr_militia',        'militia.png'),
    ('spr_militia_fight',  'militia_fight.png'),
    ('spr_archer',         'archer.png'),
    ('spr_archer_fire',    'archer_fire.png'),
    ('spr_knight',         'knight.png'),
    ('spr_knight_fight',   'knight_fight.png'),
    ('spr_spearman',       'spearman.png'),
    ('spr_spearman_fight', 'spearman_fight.png'),
]

# Building sprites
BUILDING_SPRITES = [
    ('spr_town_center',   'town_center.png',   32, 32),
    ('spr_house',         'house.png',          16, 16),
    ('spr_barracks',      'barracks.png',       32, 32),
    ('spr_archery_range', 'archery_range.png',  32, 32),
    ('spr_stable',        'stable.png',         32, 32),
    ('spr_mining_camp',   'mining_camp.png',    16, 16),
    ('spr_lumber_camp',   'lumber_camp.png',    16, 16),
]


def rgb_to_bgr555(r, g, b):
    r5 = min(31, r >> 3)
    g5 = min(31, g >> 3)
    b5 = min(31, b >> 3)
    return (b5 << 10) | (g5 << 5) | r5 | (1 << 15)


def load_terrain_tile(source_type, filename, crop):
    if source_type == 'sprites':
        path = os.path.join(SPRITES_DIR, filename)
    else:
        path = os.path.join(TERRAIN_DIR, filename)
    if not os.path.exists(path):
        print(f"  WARNING: {path} not found, using fallback")
        return np.full((TILE_PX, TILE_PX, 3), 128, dtype=np.uint8)
    img = Image.open(path).convert('RGB')
    tile = img.crop(crop)
    if tile.size != (TILE_PX, TILE_PX):
        tile = tile.resize((TILE_PX, TILE_PX), Image.NEAREST)
    return np.array(tile)


def load_sprite_rgba(filename):
    path = os.path.join(SPRITES_DIR, filename)
    if not os.path.exists(path):
        print(f"  WARNING: {path} not found, skipping")
        return None
    return np.array(Image.open(path).convert('RGBA'))


def index_pixels(flat_rgb, palette_array, offset=16):
    """Map RGB pixels to nearest palette entry. Returns array of u8 indices."""
    n = len(flat_rgb)
    indexed = np.zeros(n, dtype=np.uint8)
    chunk = 2048
    for start in range(0, n, chunk):
        end = min(start + chunk, n)
        diff = flat_rgb[start:end, np.newaxis, :].astype(np.int32) - palette_array[np.newaxis, :, :]
        dists = np.sum(diff * diff, axis=2)
        nearest = np.argmin(dists, axis=1)
        indexed[start:end] = nearest + offset
    return indexed


def main():
    os.makedirs(DATA_DIR, exist_ok=True)
    print("=== Combined Terrain + Sprite Preprocessor ===\n")

    # --- Load terrain tiles ---
    print("Loading terrain tiles...")
    tiles_rgb = [None] * NUM_TILES
    terrain_pixels = []
    for idx, source_type, filename, crop in TERRAIN_SOURCES:
        tile = load_terrain_tile(source_type, filename, crop)
        tiles_rgb[idx] = tile
        terrain_pixels.append(tile.reshape(-1, 3))
        print(f"  [{idx}] {filename}: {tile.shape[1]}x{tile.shape[0]}")

    # --- Load sprite images ---
    print("\nLoading sprites...")
    unit_data = {}
    building_data = {}
    sprite_pixels = []

    for name, filename in UNIT_SHEETS:
        data = load_sprite_rgba(filename)
        if data is None:
            continue
        unit_data[name] = data
        mask = data[:, :, 3] > 128
        sprite_pixels.append(data[mask][:, :3])
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]}")

    for name, filename, tw, th in BUILDING_SPRITES:
        data = load_sprite_rgba(filename)
        if data is None:
            continue
        if data.shape[0] > 64:
            data = data[:64, :64]
        building_data[name] = (data, tw, th)
        mask = data[:, :, 3] > 128
        sprite_pixels.append(data[mask][:, :3])
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]} -> {tw}x{th}")

    # --- Build combined palette ---
    print("\nBuilding combined palette...")
    all_terrain_px = np.vstack(terrain_pixels)
    all_sprite_px = np.vstack(sprite_pixels) if sprite_pixels else np.zeros((0, 3), dtype=np.uint8)

    # Combine all pixels, weight terrain slightly higher (seen more often)
    all_pixels = np.vstack([all_terrain_px, all_terrain_px, all_sprite_px])
    unique = np.unique(all_pixels.reshape(-1, 3), axis=0)
    print(f"  {len(unique)} unique colors total")

    max_shared = 240  # indices 16-255

    if len(unique) <= max_shared:
        palette = [tuple(c) for c in unique]
    else:
        n = len(unique)
        w = min(n, 4096)
        h = (n + w - 1) // w
        composite = Image.new('RGB', (w, h), (0, 0, 0))
        pixels = composite.load()
        for i, (r, g, b) in enumerate(unique):
            pixels[i % w, i // w] = (int(r), int(g), int(b))
        quantized = composite.quantize(colors=max_shared, method=Image.Quantize.MEDIANCUT)
        pal_flat = quantized.getpalette()
        palette = []
        seen = set()
        for i in range(max_shared):
            c = (pal_flat[i*3], pal_flat[i*3+1], pal_flat[i*3+2])
            if c not in seen:
                seen.add(c)
                palette.append(c)

    print(f"  Quantized to {len(palette)} shared colors")

    # Pad to 240
    while len(palette) < max_shared:
        palette.append((0, 0, 0))

    palette_array = np.array(palette, dtype=np.int32)

    # --- Find blue player colors for remap ---
    blues = []
    for i, (r, g, b) in enumerate(palette):
        if b > 80 and b > r + 20 and b > g + 20:
            blues.append((i, r, g, b))
    blues.sort(key=lambda x: x[3])
    blue_indices = [idx for idx, r, g, b in blues[:7]]
    print(f"  Found {len(blue_indices)} blue player color shades")

    # --- Build NDS palette binary ---
    print("\nSaving palette...")
    pal_bin = bytearray(512)
    struct.pack_into('<H', pal_bin, 0, rgb_to_bgr555(0, 0, 0))

    # UI colors at indices 1-15
    for idx, (r, g, b) in UI_PALETTE.items():
        struct.pack_into('<H', pal_bin, idx * 2, rgb_to_bgr555(r, g, b))

    # Shared colors at indices 16-255
    for i, (r, g, b) in enumerate(palette):
        struct.pack_into('<H', pal_bin, (16 + i) * 2, rgb_to_bgr555(r, g, b))

    # Save as both terrain and sprite palette (identical)
    for name in ['terrain_pal.bin', 'sprite_pal.bin']:
        path = os.path.join(DATA_DIR, name)
        with open(path, 'wb') as f:
            f.write(pal_bin)
        print(f"  {path}: {len(pal_bin)} bytes")

    # --- Build remap table for player 2 ---
    remap = bytearray(range(256))
    # Map blue palette indices to nearby red equivalents
    for bi in blue_indices:
        r, g, b = palette[bi]
        # Red variant
        red_r, red_g, red_b = min(255, b + 30), g // 3, r // 4
        # Find nearest palette entry to the red color
        target = np.array([red_r, red_g, red_b], dtype=np.int32)
        dists = np.sum((palette_array - target) ** 2, axis=1)
        nearest = np.argmin(dists)
        remap[bi + 16] = nearest + 16  # both use +16 offset

    remap_path = os.path.join(DATA_DIR, 'sprite_remap.bin')
    with open(remap_path, 'wb') as f:
        f.write(remap)
    print(f"  {remap_path}: {len(remap)} bytes")

    # --- Index terrain tiles ---
    print("\nIndexing terrain tiles...")
    all_tile_data = bytearray()
    for idx in range(NUM_TILES):
        tile = tiles_rgb[idx]
        flat = tile.reshape(-1, 3)
        indexed = index_pixels(flat, palette_array, offset=16)
        all_tile_data.extend(bytes(indexed))
        print(f"  Tile {idx}: {TILE_PX}x{TILE_PX} = {TILE_PX*TILE_PX} bytes")

    tiles_path = os.path.join(DATA_DIR, 'terrain_tiles.bin')
    with open(tiles_path, 'wb') as f:
        f.write(all_tile_data)
    print(f"  {tiles_path}: {len(all_tile_data)} bytes")

    # --- Index sprite sheets ---
    print("\nIndexing unit sprites...")
    for name, filename in UNIT_SHEETS:
        if name not in unit_data:
            continue
        data = unit_data[name]
        h, w = data.shape[:2]
        alpha = data[:, :, 3].reshape(-1)
        rgb = data[:, :, :3].reshape(-1, 3)
        opaque = alpha > 128

        indexed = np.zeros(h * w, dtype=np.uint8)
        if np.any(opaque):
            opaque_idx = np.where(opaque)[0]
            opaque_rgb = rgb[opaque_idx]
            mapped = index_pixels(opaque_rgb, palette_array, offset=16)
            indexed[opaque_idx] = mapped

        out_path = os.path.join(DATA_DIR, f'{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(bytes(indexed))
        print(f"  {name}: {w}x{h} = {len(indexed)} bytes")

    print("\nIndexing building sprites...")
    for name, filename, tw, th in BUILDING_SPRITES:
        if name not in building_data:
            continue
        data, _, _ = building_data[name]

        # Resize to target
        img = Image.fromarray(data)
        img = img.resize((tw, th), Image.NEAREST)
        data = np.array(img)

        h, w = data.shape[:2]
        alpha = data[:, :, 3].reshape(-1)
        rgb = data[:, :, :3].reshape(-1, 3)
        opaque = alpha > 128

        indexed = np.zeros(h * w, dtype=np.uint8)
        if np.any(opaque):
            opaque_idx = np.where(opaque)[0]
            opaque_rgb = rgb[opaque_idx]
            mapped = index_pixels(opaque_rgb, palette_array, offset=16)
            indexed[opaque_idx] = mapped

        out_path = os.path.join(DATA_DIR, f'{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(bytes(indexed))
        print(f"  {name}: {w}x{h} = {len(indexed)} bytes")

    # --- Summary ---
    total = sum(os.path.getsize(os.path.join(DATA_DIR, f))
                for f in os.listdir(DATA_DIR) if f.endswith('.bin'))
    print(f"\nDone! Total binary data: {total:,} bytes ({total/1024:.1f} KB)")


if __name__ == '__main__':
    main()
