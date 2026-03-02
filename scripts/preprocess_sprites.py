#!/usr/bin/env python3
"""Preprocess HD sprite PNGs into NDS-ready indexed binary data.

Creates a shared 256-color palette and indexed binary sprite data for all
unit and building sprites. Output goes to data/ directory.

Palette layout (NDS BGR555, 256 entries x 2 bytes = 512 bytes):
  Index 0:     Transparent
  Index 1-248: Shared colors from all sprites (including blue player colors)
  Index 249-255: Red player color variants (for player 2 remapping)

Also generates a 256-byte remap table for player 2 color swapping at runtime.
"""

import os
import struct
import sys
import numpy as np
from PIL import Image

SPRITES_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'sprites')
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')

# Unit sprite sheets: (output_name, filename)
# All are 160x96 (5 cols x 3 rows of 32x32 cells = 15 frames max)
UNIT_SHEETS = [
    ('spr_villager',         'villager.png'),
    ('spr_villager_walk',    'villager_walk.png'),
    ('spr_villager_attack',  'villager_attack.png'),
    ('spr_villager_f',       'villager_f.png'),
    ('spr_lumberjack',       'lumberjack.png'),
    ('spr_lumberjack_walk',  'lumberjack_walk.png'),
    ('spr_lumberjack_chop',  'lumberjack_chop.png'),
    ('spr_miner',            'miner.png'),
    ('spr_miner_walk',       'miner_walk.png'),
    ('spr_builder',          'builder.png'),
    ('spr_builder_walk',     'builder_walk.png'),
    ('spr_farmer',           'farmer.png'),
    ('spr_farmer_walk',      'farmer_walk.png'),
    ('spr_militia',          'militia.png'),
    ('spr_militia_fight',    'militia_fight.png'),
    ('spr_archer',           'archer.png'),
    ('spr_archer_fire',      'archer_fire.png'),
    ('spr_knight',           'knight.png'),
    ('spr_knight_fight',     'knight_fight.png'),
    ('spr_spearman',         'spearman.png'),
    ('spr_spearman_fight',   'spearman_fight.png'),
]

# Building sprites: (output_name, filename, target_w, target_h)
# Source PNGs are 64x64 (or 64x192 for house), scaled to game tile size
BUILDING_SPRITES = [
    ('spr_town_center',   'town_center.png',   32, 32),  # 2x2 tiles
    ('spr_house',         'house.png',          16, 16),  # 1x1 tile
    ('spr_barracks',      'barracks.png',       32, 32),  # 2x2 tiles
    ('spr_archery_range', 'archery_range.png',  32, 32),  # 2x2 tiles
    ('spr_stable',        'stable.png',         32, 32),  # 2x2 tiles
    ('spr_mining_camp',   'mining_camp.png',    16, 16),  # 1x1 tile
    ('spr_lumber_camp',   'lumber_camp.png',    16, 16),  # 1x1 tile
]


def load_rgba(filename):
    """Load a PNG as RGBA numpy array."""
    path = os.path.join(SPRITES_DIR, filename)
    if not os.path.exists(path):
        print(f"  WARNING: {path} not found, skipping")
        return None
    img = Image.open(path).convert('RGBA')
    return np.array(img)


def build_shared_palette(all_rgba_images, max_colors=248):
    """Build a shared quantized palette from all sprite images.

    Returns list of (R, G, B) tuples, length <= max_colors.
    """
    # Collect all opaque pixels into one flat array
    pixel_lists = []
    for img_data in all_rgba_images:
        mask = img_data[:, :, 3] > 128
        rgb = img_data[mask][:, :3]
        pixel_lists.append(rgb)

    all_pixels = np.vstack(pixel_lists)
    unique_colors = np.unique(all_pixels.reshape(-1, 3), axis=0)
    print(f"  {len(all_pixels)} opaque pixels, {len(unique_colors)} unique colors")

    if len(unique_colors) <= max_colors:
        return [tuple(c) for c in unique_colors]

    # Create a composite image with all unique colors for PIL quantization
    n = len(unique_colors)
    w = min(n, 4096)
    h = (n + w - 1) // w
    composite = Image.new('RGB', (w, h), (0, 0, 0))
    pixels = composite.load()
    for i, (r, g, b) in enumerate(unique_colors):
        pixels[i % w, i // w] = (int(r), int(g), int(b))

    quantized = composite.quantize(colors=max_colors, method=Image.Quantize.MEDIANCUT)
    pal_flat = quantized.getpalette()

    palette = []
    for i in range(max_colors):
        palette.append((pal_flat[i * 3], pal_flat[i * 3 + 1], pal_flat[i * 3 + 2]))

    # Remove duplicate colors
    seen = set()
    deduped = []
    for c in palette:
        if c not in seen:
            seen.add(c)
            deduped.append(c)
    return deduped


def index_rgba_image(img_data, palette_array, target_size=None):
    """Convert RGBA image to indexed format using the shared palette.

    Returns (indexed_bytes, width, height).
    Index 0 = transparent, 1-N = palette color.
    """
    if target_size:
        img = Image.fromarray(img_data)
        img = img.resize(target_size, Image.NEAREST)
        img_data = np.array(img)

    h, w = img_data.shape[:2]
    alpha = img_data[:, :, 3].reshape(-1)
    rgb = img_data[:, :, :3].reshape(-1, 3).astype(np.int32)

    # Vectorized nearest-color lookup
    opaque_mask = alpha > 128
    indexed = np.zeros(h * w, dtype=np.uint8)

    if np.any(opaque_mask):
        opaque_rgb = rgb[opaque_mask]
        # Compute distances to all palette colors (N_opaque x N_palette)
        # Process in chunks to avoid memory issues
        chunk_size = 4096
        opaque_indices = np.where(opaque_mask)[0]

        for start in range(0, len(opaque_indices), chunk_size):
            end = min(start + chunk_size, len(opaque_indices))
            chunk_rgb = opaque_rgb[start:end].astype(np.int32)

            # Broadcast: (chunk, 1, 3) - (1, palette, 3) -> (chunk, palette, 3)
            diff = chunk_rgb[:, np.newaxis, :] - palette_array[np.newaxis, :, :]
            dists = np.sum(diff * diff, axis=2)
            nearest = np.argmin(dists, axis=1).astype(np.uint8)

            # +1 to reserve index 0 for transparent
            indexed[opaque_indices[start:end]] = nearest + 1

    return bytes(indexed), w, h


def find_blue_player_indices(palette):
    """Find palette entries that correspond to blue player colors.

    AoE2 player 1 colors are shades of blue used for unit/building accents.
    Returns list of palette indices (0-based, before +1 offset).
    """
    blues = []
    for i, (r, g, b) in enumerate(palette):
        # Blue-dominant: B channel is significantly higher than R and G
        if b > 80 and b > r + 20 and b > g + 20:
            blues.append((i, r, g, b))

    # Sort by blue intensity (darkest to brightest)
    blues.sort(key=lambda x: x[3])

    # Take up to 7 shades (we have 7 slots: indices 249-255)
    return [idx for idx, r, g, b in blues[:7]]


def rgb_to_bgr555(r, g, b):
    """Convert RGB888 to NDS BGR555 format (16-bit)."""
    r5 = min(31, r >> 3)
    g5 = min(31, g >> 3)
    b5 = min(31, b >> 3)
    return (b5 << 10) | (g5 << 5) | r5 | (1 << 15)


def main():
    os.makedirs(DATA_DIR, exist_ok=True)

    print("=== NDS Sprite Preprocessor ===\n")

    # --- Load all images ---
    print("Loading sprites...")
    all_images = []
    unit_data = {}
    building_data = {}

    for name, filename in UNIT_SHEETS:
        data = load_rgba(filename)
        if data is None:
            continue
        unit_data[name] = data
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]}")

    for name, filename, tw, th in BUILDING_SPRITES:
        data = load_rgba(filename)
        if data is None:
            continue
        # Crop to first 64x64 frame if multi-frame (e.g., house.png is 64x192)
        if data.shape[0] > 64:
            data = data[:64, :64]
        building_data[name] = (data, tw, th)
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]} -> {tw}x{th}")

    if not all_images:
        print("ERROR: No sprite images found!")
        sys.exit(1)

    # --- Build shared palette ---
    print("\nBuilding shared palette...")
    palette = build_shared_palette(all_images, max_colors=248)
    print(f"  Quantized to {len(palette)} colors")

    # --- Player color handling ---
    blue_indices = find_blue_player_indices(palette)
    print(f"  Found {len(blue_indices)} blue player color shades")

    # Create red variants of blue player colors
    red_remap = {}  # maps NDS index (1-based) to red NDS index
    red_colors = []
    for bi in blue_indices:
        r, g, b = palette[bi]
        # Create red variant: high red, low blue/green
        red_r = min(255, b + 30)
        red_g = g // 3
        red_b = r // 4
        red_colors.append((red_r, red_g, red_b))

    # Add red colors to palette
    red_start_idx = len(palette)
    for i, rc in enumerate(red_colors):
        palette.append(rc)
        # Map blue NDS index (bi+1) to red NDS index (red_start_idx+i+1)
        red_remap[blue_indices[i] + 1] = red_start_idx + i + 1

    # Pad palette to 255 entries
    while len(palette) < 255:
        palette.append((0, 0, 0))

    print(f"  Final palette: {len(palette)} colors + transparent (index 0)")
    print(f"  Red remap: {red_remap}")

    # --- Save NDS palette (BGR555, 256 entries x 2 bytes = 512 bytes) ---
    print("\nSaving palette...")
    pal_bin = bytearray(512)
    # Index 0 = transparent (value doesn't matter, but use 0)
    struct.pack_into('<H', pal_bin, 0, 0)
    for i, (r, g, b) in enumerate(palette):
        val = rgb_to_bgr555(r, g, b)
        struct.pack_into('<H', pal_bin, (i + 1) * 2, val)

    pal_path = os.path.join(DATA_DIR, 'sprite_pal.bin')
    with open(pal_path, 'wb') as f:
        f.write(pal_bin)
    print(f"  {pal_path}: {len(pal_bin)} bytes")

    # --- Save red remap table (256 bytes, identity except for blue->red) ---
    remap = bytearray(range(256))
    for src, dst in red_remap.items():
        if src < 256 and dst < 256:
            remap[src] = dst

    remap_path = os.path.join(DATA_DIR, 'sprite_remap.bin')
    with open(remap_path, 'wb') as f:
        f.write(remap)
    print(f"  {remap_path}: {len(remap)} bytes")

    # --- Prepare palette array for indexing ---
    palette_array = np.array(palette, dtype=np.int32)

    # --- Process unit sprite sheets ---
    print("\nProcessing unit sprites...")
    for name, filename in UNIT_SHEETS:
        if name not in unit_data:
            continue
        data = unit_data[name]
        indexed, w, h = index_rgba_image(data, palette_array)
        out_path = os.path.join(DATA_DIR, f'{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  {name}: {w}x{h} = {len(indexed)} bytes")

    # --- Process building sprites ---
    print("\nProcessing building sprites...")
    for name, filename, tw, th in BUILDING_SPRITES:
        if name not in building_data:
            continue
        data, tw2, th2 = building_data[name]
        indexed, w, h = index_rgba_image(data, palette_array, (tw, th))
        out_path = os.path.join(DATA_DIR, f'{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  {name}: {w}x{h} = {len(indexed)} bytes")

    # --- Summary ---
    total_size = 0
    for f in os.listdir(DATA_DIR):
        if f.endswith('.bin'):
            total_size += os.path.getsize(os.path.join(DATA_DIR, f))

    print(f"\nDone! Total binary data: {total_size:,} bytes ({total_size/1024:.1f} KB)")
    print(f"Output directory: {DATA_DIR}")


if __name__ == '__main__':
    main()
