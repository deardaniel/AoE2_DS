#!/usr/bin/env python3
"""Preprocess HD sprite PNGs into NDS-ready indexed binary data.

Creates a shared 256-color palette and indexed binary sprite data for all
unit and building sprites. Output goes to data/ directory.

Palette layout (NDS BGR555, 256 entries x 2 bytes = 512 bytes):
  Index 0:      Transparent
  Index 1-15:   Reserved for UI colors (set at runtime by terrain_initPalette)
  Index 16-N:   Shared colors from all sprites (including blue player colors)
  Index N+1...: Red player color variants (for player 2 remapping)

Also generates a 256-byte remap table for player 2 color swapping at runtime.
"""

import os
import struct
import sys
import numpy as np
from PIL import Image
from shared_constants import ISO_TILE_H, footprint_height, rgb_to_bgr555

SPRITES_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'sprites')
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')

# Unit sprite sheets: (output_name, filename)
# All are 160x96 (5 cols x 3 rows of 32x32 cells = 15 frames max)
UNIT_SHEETS = [
    ('spr_villager',         'villager.png'),
    ('spr_villager_walk',    'villager_walk.png'),
    ('spr_villager_attack',  'villager_attack.png'),
    ('spr_villager_f',       'villager_f.png'),
    ('spr_villager_carry',   'villager_carry.png'),
    ('spr_lumberjack',       'lumberjack.png'),
    ('spr_lumberjack_walk',  'lumberjack_walk.png'),
    ('spr_lumberjack_work',  'lumberjack_work.png'),
    ('spr_lumberjack_carry', 'lumberjack_carry.png'),
    ('spr_miner',            'miner.png'),
    ('spr_miner_walk',       'miner_walk.png'),
    ('spr_miner_work',       'miner_work.png'),
    ('spr_miner_carry',      'miner_carry.png'),
    ('spr_builder',          'builder.png'),
    ('spr_builder_walk',     'builder_walk.png'),
    ('spr_builder_work',     'builder_work.png'),
    ('spr_farmer',           'farmer.png'),
    ('spr_farmer_walk',      'farmer_walk.png'),
    ('spr_farmer_work',      'farmer_work.png'),
    ('spr_farmer_carry',     'farmer_carry.png'),
    ('spr_forager_carry',    'forager_carry.png'),
    ('spr_militia',          'militia.png'),
    ('spr_militia_walk',     'militia_walk.png'),
    ('spr_militia_fight',    'militia_fight.png'),
    ('spr_militia_die',      'militia_die.png'),
    ('spr_archer',           'archer.png'),
    ('spr_archer_walk',      'archer_walk.png'),
    ('spr_archer_fire',      'archer_fire.png'),
    ('spr_archer_die',       'archer_die.png'),
    ('spr_knight',           'knight.png'),
    ('spr_knight_walk',      'knight_walk.png'),
    ('spr_knight_fight',     'knight_fight.png'),
    ('spr_knight_die',       'knight_die.png'),
    ('spr_spearman',         'spearman.png'),
    ('spr_spearman_walk',    'spearman_walk.png'),
    ('spr_spearman_fight',   'spearman_fight.png'),
    ('spr_spearman_die',     'spearman_die.png'),
    ('spr_scout',            'scout.png'),
    ('spr_scout_walk',       'scout_walk.png'),
    ('spr_scout_die',        'scout_die.png'),
    ('spr_villager_die',     'villager_die.png'),
    ('spr_sheep_stand',      'sheep_stand.png'),
    ('spr_sheep_walk',       'sheep_walk.png'),
    ('spr_sheep_die',        'sheep_die.png'),
]

# Building sprites: (output_name, filename, target_w, target_h, tileW, tileH, hotspot_y_ratio)
# Isometric building sizes based on tile footprint:
#   footprint_w = (tileW + tileH) * 16, footprint_h = (tileW + tileH) * 8
#   hotspot_y_ratio = fraction from top of content where ground level is (from SLP data)
# Content is positioned so the hotspot aligns with (ph - footH) in the canvas,
# making the building rise above the terrain footprint naturally.
BUILDING_SPRITES = [
    ('spr_town_center',   'town_center.png',  128,  96, 4, 4, 0.536), # 4x4: TC composite with shadows
    ('spr_house',         'house.png',          32,  32, 1, 1, 0.613), # 1x1: SLP 2223, hotspot 73/119
    ('spr_barracks',      'barracks.png',       64,  64, 2, 2, 0.681), # 2x2: SLP 2683, hotspot 141/207
    ('spr_archery_range', 'archery_range.png',  64,  80, 2, 2, 0.708), # 2x2: SLP 21, hotspot 179/253
    ('spr_stable',        'stable.png',         64,  64, 2, 2, 0.663), # 2x2: SLP 1009, hotspot 134/202
    ('spr_mining_camp',   'mining_camp.png',    32,  32, 1, 1, 0.655), # 1x1: SLP 3492, hotspot 72/110
    ('spr_lumber_camp',   'lumber_camp.png',    32,  48, 1, 1, 0.715), # 1x1: SLP 3504, hotspot 98/137
    ('spr_wall',          'wall.png',           32,  32, 1, 1, 0.807), # 1x1: SLP 2099, hotspot 71/88
    ('spr_tower',         'tower.png',          32,  64, 1, 1, 0.894), # 1x1: SLP 2652, hotspot 202/226
    ('spr_market',        'market.png',         64,  80, 2, 2, 0.714), # 2x2: SLP 2278, hotspot 220/308
    ('spr_castle',        'castle.png',         96, 128, 3, 3, 0.798), # 3x3: SLP 305, hotspot 280/351
]

# Icon sprites for build menu: (output_name, filename, target_w, target_h, cols, rows, scale)
# 36x36 source icons scaled to 32x32 output (single frame, no directions)
ICON_SPRITES = [
    ('icon_tc',            'icon_tc.png',            32, 32, 1, 1, 0.889),
    ('icon_house',         'icon_house.png',         32, 32, 1, 1, 0.889),
    ('icon_barracks',      'icon_barracks.png',      32, 32, 1, 1, 0.889),
    ('icon_archery_range', 'icon_archery_range.png', 32, 32, 1, 1, 0.889),
    ('icon_stable',        'icon_stable.png',        32, 32, 1, 1, 0.889),
    ('icon_farm',          'icon_farm.png',           32, 32, 1, 1, 0.889),
    ('icon_mining_camp',   'icon_mining_camp.png',   32, 32, 1, 1, 0.889),
    ('icon_lumber_camp',   'icon_lumber_camp.png',   32, 32, 1, 1, 0.889),
    ('icon_wall',          'icon_wall.png',          32, 32, 1, 1, 0.889),
    ('icon_tower',         'icon_tower.png',         32, 32, 1, 1, 0.889),
    ('icon_market',        'icon_market.png',        32, 32, 1, 1, 0.889),
    ('icon_castle',        'icon_castle.png',        32, 32, 1, 1, 0.889),
]

# Resource object sprites: (output_name, filename)
# Single-frame 32x32 sprites for resource tile overlays
RESOURCE_SPRITES = [
    ('spr_tree',       'tree.png'),
    ('spr_gold_mine',  'gold_mine.png'),
    ('spr_stone_mine', 'stone_mine.png'),
    ('spr_fire',       'fire_small.png'),
    ('spr_berries',    'berries.png'),
]


def load_rgba(filename):
    """Load a PNG as RGBA numpy array."""
    path = os.path.join(SPRITES_DIR, filename)
    if not os.path.exists(path):
        print(f"  WARNING: {path} not found, skipping")
        return None
    img = Image.open(path).convert('RGBA')
    return np.array(img)


def find_player_colors(all_rgba_images):
    """Find blue player-color pixels across all sprites.

    AoE2 player 1 (blue) colors are saturated blues used for team indicators.
    Only match clearly saturated player blues, not every vaguely-blue pixel.
    Returns list of (R, G, B) tuples to force into the palette.
    """
    player_colors = set()
    for img_data in all_rgba_images:
        mask = img_data[:, :, 3] > 128
        rgb = img_data[mask][:, :3]
        for r, g, b in rgb:
            r, g, b = int(r), int(g), int(b)
            # Saturated player blues: high blue, very low red, blue dominates
            if b > 120 and r < 80 and g < 120 and (b - r) > 80:
                player_colors.add((r, g, b))
    return sorted(player_colors, key=lambda c: c[2])


def build_shared_palette(all_rgba_images, max_colors=248):
    """Build a shared quantized palette from all sprite images.

    Returns list of (R, G, B) tuples, length <= max_colors.
    Reserves the 8 AoE2 player 1 colors (from 50500.bina indices 16-23)
    at the START of the palette to ensure correct player color handling.
    """
    # AoE2 player 1 blue colors (50500.bina palette indices 16-23)
    PLAYER_COLORS = [
        (0, 0, 82),       # very dark blue
        (0, 21, 130),      # dark blue
        (19, 49, 161),     # medium blue
        (48, 93, 182),     # medium-light blue
        (74, 121, 208),    # light blue
        (110, 166, 235),   # sky blue
        (151, 206, 255),   # very light blue
        (205, 250, 255),   # near white cyan
    ]

    # Collect all opaque pixels into one flat array
    pixel_lists = []
    for img_data in all_rgba_images:
        mask = img_data[:, :, 3] > 128
        rgb = img_data[mask][:, :3]
        pixel_lists.append(rgb)

    all_pixels = np.vstack(pixel_lists)
    unique_colors = np.unique(all_pixels.reshape(-1, 3), axis=0)
    print(f"  {len(all_pixels)} opaque pixels, {len(unique_colors)} unique colors")

    # Reserve player colors, quantize rest with reduced budget
    reserved = set(PLAYER_COLORS)
    quant_budget = max_colors - len(PLAYER_COLORS)

    if len(unique_colors) <= quant_budget:
        palette = list(PLAYER_COLORS) + [tuple(c) for c in unique_colors if tuple(c) not in reserved]
        return palette

    # Create a composite image with all unique colors for PIL quantization
    n = len(unique_colors)
    w = min(n, 4096)
    h = (n + w - 1) // w
    composite = Image.new('RGB', (w, h), (0, 0, 0))
    pixels = composite.load()
    for i, (r, g, b) in enumerate(unique_colors):
        pixels[i % w, i // w] = (int(r), int(g), int(b))

    quantized = composite.quantize(colors=quant_budget, method=Image.Quantize.MEDIANCUT)
    pal_flat = quantized.getpalette()

    quant_colors = []
    for i in range(quant_budget):
        quant_colors.append((pal_flat[i * 3], pal_flat[i * 3 + 1], pal_flat[i * 3 + 2]))

    # Build final palette: player colors first, then quantized (no duplicates)
    palette = list(PLAYER_COLORS)
    seen = set(PLAYER_COLORS)
    for c in quant_colors:
        if c not in seen:
            seen.add(c)
            palette.append(c)

    return palette


def index_rgba_image(img_data, palette_array, target_size=None, hotspot_align=None):
    """Convert RGBA image to indexed format using the shared palette.

    Returns (indexed_bytes, width, height).
    Index 0 = transparent, 16-N = palette color (1-15 reserved for UI).

    If hotspot_align=(tileW, tileH, hotspot_y_ratio): auto-crops transparent
    borders, scales to fit, and positions content so that the ground level
    (at hotspot_y_ratio from top of content) aligns with the top of the
    isometric footprint area in the canvas.
    """
    if target_size:
        tw, th = target_size
        img = Image.fromarray(img_data)

        if hotspot_align:
            tileW, tileH, hotspot_ratio = hotspot_align
            footH = footprint_height(tileW, tileH)
            # SLP hotspot marks the CENTER of the footprint diamond, so align
            # with the center of the footprint zone in the canvas
            ground_row = th - footH // 2  # canvas row = footprint center

            # Auto-crop transparent borders to get just the building content
            bbox = img.split()[3].getbbox()
            if bbox:
                img = img.crop(bbox)

            # Scale proportionally to fit within target
            scale = min(tw / img.width, th / img.height)
            new_w = int(img.width * scale)
            new_h = int(img.height * scale)
            img = img.resize((new_w, new_h), Image.LANCZOS)

            if hotspot_ratio > 0:
                # Position so hotspot (ground level) aligns with ground_row
                hotspot_y = int(hotspot_ratio * new_h)
                ox = (tw - new_w) // 2
                oy = ground_row - hotspot_y  # content top, may be negative (clipped)
            else:
                # No hotspot: center horizontally, place at top of canvas
                ox = (tw - new_w) // 2
                oy = 0

            result = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
            result.paste(img, (ox, oy))
            img_data = np.array(result)
        else:
            # Default: scale and center
            scale = min(tw / img.width, th / img.height)
            new_w = int(img.width * scale)
            new_h = int(img.height * scale)
            img = img.resize((new_w, new_h), Image.LANCZOS)

            result = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
            ox = (tw - new_w) // 2
            oy = (th - new_h) // 2
            result.paste(img, (ox, oy))
            img_data = np.array(result)

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

            # +16 to reserve indices 0-15 (0=transparent, 1-15=UI colors)
            indexed[opaque_indices[start:end]] = nearest + 16

    return bytes(indexed), w, h


def find_blue_player_indices(palette):
    """Find palette entries that correspond to blue player colors.

    The first 8 entries in the palette are the reserved AoE2 player 1 colors
    (from 50500.bina indices 16-23). Returns those indices directly.
    """
    # Player colors are reserved at palette indices 0-7 by build_shared_palette()
    return list(range(8))


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

    for name, filename, tw, th, tileW, tileH, hotspot_ratio in BUILDING_SPRITES:
        data = load_rgba(filename)
        if data is None:
            continue
        # Crop to first frame if multi-frame sprite sheet (height > width)
        # e.g., house.png is 64x192 (3 stacked frames), take first 64x64
        if data.shape[0] > data.shape[1]:
            frame_h = data.shape[1]  # assume square frames
            data = data[:frame_h, :frame_h]
        building_data[name] = (data, tw, th, tileW, tileH, hotspot_ratio)
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]} -> {tw}x{th}")

    icon_data = {}
    for name, filename, tw, th, cols, rows, scale in ICON_SPRITES:
        data = load_rgba(filename)
        if data is None:
            continue
        icon_data[name] = (data, tw, th)
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]} -> {tw}x{th}")

    resource_data = {}
    for name, filename in RESOURCE_SPRITES:
        data = load_rgba(filename)
        if data is None:
            continue
        resource_data[name] = data
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]}")

    # Load terrain texture samples so greens/blues are represented in palette
    # Use large samples (256x256) to give terrain colors proper weight
    TERRAIN_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/terrain/textures'
    terrain_files = ['g_grs_00_color.png', 'g_for_00_color.png', 'g_wtr_00_color.png',
                     'g_rd1_00_color.png', 'g_des_00_color.png',
                     'g_gr2_00_color.png', 'g_gr3_00_color.png', 'g_gr6_00_color.png']
    for tf in terrain_files:
        tp = os.path.join(TERRAIN_DIR, tf)
        if os.path.exists(tp):
            timg = Image.open(tp).convert('RGBA')
            # Sample a 256x256 region to give terrain adequate palette weight
            crop = timg.crop((128, 128, 384, 384))
            tdata = np.array(crop)
            all_images.append(tdata)
            print(f"  terrain/{tf}: sampled 256x256")

    if not all_images:
        print("ERROR: No sprite images found!")
        sys.exit(1)

    # --- Build shared palette ---
    print("\nBuilding shared palette...")
    palette = build_shared_palette(all_images, max_colors=230)
    print(f"  Quantized to {len(palette)} colors")

    # --- Player color handling ---
    blue_indices = find_blue_player_indices(palette)
    print(f"  Found {len(blue_indices)} blue player color shades")

    # Create red variants of blue player colors
    red_remap = {}  # maps NDS index (16-based) to red NDS index
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
        # Map blue NDS index (bi+16) to red NDS index (red_start_idx+i+16)
        red_remap[blue_indices[i] + 16] = red_start_idx + i + 16

    # Pad palette to 240 entries (indices 16-255 = 240 slots)
    while len(palette) < 240:
        palette.append((0, 0, 0))

    print(f"  Final palette: {len(palette)} colors + transparent (index 0)")
    print(f"  Red remap: {red_remap}")

    # --- Save NDS palette (BGR555, 256 entries x 2 bytes = 512 bytes) ---
    print("\nSaving palette...")
    pal_bin = bytearray(512)
    # Indices 0-15 reserved (0=transparent, 1-15=UI colors set at runtime)
    for i, (r, g, b) in enumerate(palette):
        val = rgb_to_bgr555(r, g, b)
        struct.pack_into('<H', pal_bin, (i + 16) * 2, val)

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

    # --- Prepare palette array for indexing (exclude red variants and padding) ---
    palette_array = np.array(palette[:red_start_idx], dtype=np.int32)

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
    for name, filename, tw, th, tileW, tileH, hotspot_ratio in BUILDING_SPRITES:
        if name not in building_data:
            continue
        data, tw2, th2, tw3, th3, hr = building_data[name]
        indexed, w, h = index_rgba_image(data, palette_array, (tw, th),
                                          hotspot_align=(tileW, tileH, hotspot_ratio))
        out_path = os.path.join(DATA_DIR, f'{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  {name}: {w}x{h} = {len(indexed)} bytes")

    # --- Process icon sprites ---
    print("\nProcessing icon sprites...")
    for name, filename, tw, th, cols, rows, scale in ICON_SPRITES:
        if name not in icon_data:
            continue
        data, tw2, th2 = icon_data[name]
        indexed, w, h = index_rgba_image(data, palette_array, (tw, th))
        out_path = os.path.join(DATA_DIR, f'{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  {name}: {w}x{h} = {len(indexed)} bytes")

    # --- Process resource sprites ---
    print("\nProcessing resource sprites...")
    for name, filename in RESOURCE_SPRITES:
        if name not in resource_data:
            continue
        data = resource_data[name]
        indexed, w, h = index_rgba_image(data, palette_array)
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
