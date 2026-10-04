#!/usr/bin/env python3
"""Preprocess sprite PNGs into NDS-ready indexed binary data.

The palette is the game's own (50500.bina), so sprite colours are exact: every
sprite pixel is already one of its 256 entries, and nothing is quantised.

Palette layout (NDS BGR555, 256 entries x 2 bytes = 512 bytes):
  Index 0:      Transparent
  Index 1-15:   UI colours (set at runtime by terrain_initPalette); index 1
                doubles as the shadow marker in sprite data
  Index 16-255: The game palette's entries 16-255, in place. Entries 0-15 of
                the game palette that our sprites use are moved into entries
                nothing uses.

Player colours are the game's ramps: player 1 at 16-23, player 2 at 32-39.
sprite_remap.bin maps one onto the other for the second player.
"""

import json
import os
import struct
import sys
import numpy as np
from PIL import Image
from downscale import reduce_exact, reduce_layers
from shared_constants import rgb_to_bgr555, TC_CANVAS, TC_ANCHOR, PAL_SHADOW

SPRITES_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'sprites')
DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')
GAME_PALETTE = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/interface/50500.bina'

# Unit sprite sheets: (output_name, filename) — built by build_unit_sheets.py,
# which documents the layouts
UNIT_SHEETS = [
    ('spr_villager',         'villager.png'),
    ('spr_villager_walk',    'villager_walk.png'),
    ('spr_villager_attack',  'villager_attack.png'),
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
    ('spr_scout_fight',      'scout_fight.png'),
    ('spr_scout_die',        'scout_die.png'),
    ('spr_villager_die',     'villager_die.png'),
    ('spr_sheep_stand',      'sheep_stand.png'),
    ('spr_sheep_walk',       'sheep_walk.png'),
    ('spr_sheep_die',        'sheep_die.png'),
    ('spr_ram_stand',        'ram_stand.png'),
    ('spr_ram_walk',         'ram_walk.png'),
    ('spr_ram_fight',        'ram_fight.png'),
    ('spr_ram_die',          'ram_death.png'),
    ('spr_mango_stand',      'mango_stand.png'),
    ('spr_mango_walk',       'mango_walk.png'),
    ('spr_mango_fight',      'mango_fight.png'),
    ('spr_mango_die',        'mango_death.png'),
    ('spr_monk_stand',       'monk_stand.png'),
    ('spr_monk_walk',        'monk_walk.png'),
    ('spr_monk_fight',       'monk_fight.png'),
    ('spr_monk_die',         'monk_death.png'),
]

# Building sprites in BuildingTypeId order (source/config.h); None = no sprite.
# Each PNG is a raw SLP frame with a sidecar .json holding its hotspot
# (extract_buildings.py), and some a <name>_shadow.png. It is reduced by
# exactly 3 around the hotspot, which the renderer puts on the centre of the
# building's footprint; shadow pixels become the PAL_SHADOW marker.
BUILDING_SPRITES = [
    'town_center',  # composite_tc.py output, already reduced: see PREALIGNED
    'house', 'barracks', 'archery_range', 'stable',
    None,           # farm: drawn as terrain
    'mining_camp', 'lumber_camp', 'wall', 'tower', 'market', 'castle',
    'monastery', 'university',
]

# Finished canvases (composite_tc.py): indexed as-is, semi-transparent pixels
# become the PAL_SHADOW marker. name -> (canvas size, anchor)
PREALIGNED = {'town_center': (TC_CANVAS, TC_ANCHOR)}

# Construction sites by footprint size (1x1 .. 4x4), three stages each,
# stored as one sheet per size with the stages stacked vertically.
CONSTRUCTION_SIZES = [1, 2, 3, 4]
CONSTRUCTION_STAGES = 3

GEOM_HEADER = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           'source', 'sprite_geom.h')

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
    ('icon_monastery',     'icon_monastery.png',     32, 32, 1, 1, 0.889),
    ('icon_university',    'icon_university.png',    32, 32, 1, 1, 0.889),
    # Unit icons for the train menu (extract_icons.py)
    ('icon_unit_villager', 'icon_unit_villager.png', 32, 32, 1, 1, 0.889),
    ('icon_unit_militia',  'icon_unit_militia.png',  32, 32, 1, 1, 0.889),
    ('icon_unit_archer',   'icon_unit_archer.png',   32, 32, 1, 1, 0.889),
    ('icon_unit_knight',   'icon_unit_knight.png',   32, 32, 1, 1, 0.889),
    ('icon_unit_spearman', 'icon_unit_spearman.png', 32, 32, 1, 1, 0.889),
    ('icon_unit_scout',    'icon_unit_scout.png',    32, 32, 1, 1, 0.889),
    ('icon_unit_ram',      'icon_unit_ram.png',      32, 32, 1, 1, 0.889),
    ('icon_unit_mangonel', 'icon_unit_mangonel.png', 32, 32, 1, 1, 0.889),
    ('icon_unit_monk',     'icon_unit_monk.png',     32, 32, 1, 1, 0.889),
]

# Single-frame overlay sprites: (output_name, filename)
RESOURCE_SPRITES = [
    ('spr_fire',       'fire_small.png'),
]

# Resource sheets (build_resource_sheets.py): variants side by side, already
# reduced, shadow as semi-transparent pixels. name -> sprites/res_<name>.png,
# geometry in sprites/resources.json.
RESOURCE_SHEETS = ['tree', 'gold', 'stone', 'berries']


def load_hotspot(name):
    """Return the SLP hotspot (x, y) from a sprite's sidecar .json."""
    path = os.path.join(SPRITES_DIR, name + '.json')
    if not os.path.exists(path):
        sys.exit(f"{path} missing: run scripts/extract_buildings.py")
    with open(path) as f:
        meta = json.load(f)
    return meta['hotspotX'], meta['hotspotY']


def load_rgba(filename, required=True):
    """Load a PNG as RGBA numpy array."""
    path = os.path.join(SPRITES_DIR, filename)
    if not os.path.exists(path):
        if required:
            print(f"  WARNING: {path} not found, skipping")
        return None
    img = Image.open(path).convert('RGBA')
    return np.array(img)


def load_game_palette():
    """The game's 256-colour palette as a list of (r, g, b)."""
    with open(GAME_PALETTE) as f:
        tokens = f.read().split()
    if tokens[0] != 'JASC-PAL':
        sys.exit(f"{GAME_PALETTE} is not a JASC palette")
    n = int(tokens[2])
    vals = [int(t) for t in tokens[3:3 + n * 3]]
    return [tuple(vals[i * 3:i * 3 + 3]) for i in range(n)]


def build_palette(all_rgba_images):
    """The 240 colours for NDS indices 16-255.

    Slot i holds game palette entry i + 16. The game's entries 0-15 can't
    keep their place (0 is transparent on the NDS and 1-15 are UI colours),
    so the ones our sprites use are moved into entries no sprite uses.
    """
    game = load_game_palette()
    index_of = {}
    for i, c in enumerate(game):
        index_of.setdefault(c, i)

    used = set()
    for img in all_rgba_images:
        for c in np.unique(img[img[:, :, 3] == 255][:, :3], axis=0):
            c = tuple(int(v) for v in c)
            if c in index_of:
                used.add(index_of[c])

    palette = list(game[16:256])
    # A colour that also exists at 16+ (the game palette repeats black and
    # white) needs no move
    high = set(palette)
    movers = [i for i in sorted(used) if i < 16 and game[i] not in high]
    # Never reuse the player ramps: the second player's remap targets 32-39
    free = [i for i in range(40, 256) if i not in used]
    if len(movers) > len(free):
        sys.exit(f"{len(movers)} low palette entries in use but only {len(free)} free slots")
    for src, dst in zip(movers, free):
        palette[dst - 16] = game[src]
    print(f"  {len(used)} game palette entries in use; moved {len(movers)} low entries "
          f"into free slots, {len(free) - len(movers)} left")
    return palette


def index_rgba_image(img_data, palette_array, target_size=None, prealigned=False):
    """Convert RGBA image to indexed format using the shared palette.

    Returns (indexed_bytes, width, height).
    Index 0 = transparent, 16-N = palette color (1-15 reserved for UI).

    prealigned: semi-transparent pixels become the PAL_SHADOW marker.
    target_size: scale to fit and centre (icons only — sprites are never
    rescaled here).
    """
    if target_size:
        tw, th = target_size
        # Premultiplied alpha so edge pixels don't blend toward black
        img = Image.fromarray(img_data).convert('RGBa')
        scale = min(tw / img.width, th / img.height)
        new_w = int(img.width * scale)
        new_h = int(img.height * scale)
        img = img.resize((new_w, new_h), Image.LANCZOS).convert('RGBA')
        result = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
        result.paste(img, ((tw - new_w) // 2, (th - new_h) // 2))
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

    if prealigned:
        indexed[(alpha > 0) & ~opaque_mask] = PAL_SHADOW

    return bytes(indexed), w, h


def write_geom_header(building_geom, construction_geom):
    """Write source/sprite_geom.h: sizes and anchors the renderer needs."""
    with open(os.path.join(SPRITES_DIR, 'units.json')) as f:
        units = json.load(f)
    png_to_bin = {filename: name for name, filename in UNIT_SHEETS}
    lines = [
        '// Generated by scripts/preprocess_sprites.py — do not edit.',
        '// Sprite sizes and anchors, measured from the sprites themselves.',
        '#pragma once',
        '#include <nds.h>',
        '',
        '// A building sprite: canvas size and the pixel that sits on the centre',
        "// of the building's footprint diamond.",
        'struct SpriteGeom { s16 w, h, ax, ay; };',
        '',
        "// A unit sheet: cell size, the cell pixel of the unit's ground position,",
        '// columns in the sheet, frames per direction, and whether the animation',
        '// plays once (deaths) or loops.',
        'struct SheetGeom { u8 cw, ch, ax, ay, cols, fpd; bool once; };',
        '',
        '// Indexed by BuildingTypeId',
        'static const SpriteGeom BLDG_GEOM[] = {',
    ]
    for name, g in zip(BUILDING_SPRITES, building_geom):
        lines.append(f'    {{ {g[0]:3d}, {g[1]:3d}, {g[2]:3d}, {g[3]:3d} }},  // {name or "farm (terrain)"}')
    lines += ['};', '', '// Construction sites, indexed by footprint size - 1; the sheet holds',
              f'// {CONSTRUCTION_STAGES} stages stacked vertically.',
              f'static const int CONSTRUCTION_STAGES = {CONSTRUCTION_STAGES};',
              'static const SpriteGeom CONSTRUCTION_GEOM[] = {']
    for size, g in zip(CONSTRUCTION_SIZES, construction_geom):
        lines.append(f'    {{ {g[0]:3d}, {g[1]:3d}, {g[2]:3d}, {g[3]:3d} }},  // {size}x{size}')
    with open(os.path.join(SPRITES_DIR, 'resources.json')) as f:
        resources = json.load(f)
    lines += ['};', '', '// Resource sheets (data/spr_res_*.bin): `count` variants side by side,',
              '// each a cw x ch cell with (ax, ay) on the centre of its tile.',
              'struct ResGeom { u8 cw, ch, ax, ay, count; };']
    for name in RESOURCE_SHEETS:
        g = resources[name]
        lines.append(f'static const ResGeom RES_GEOM_{name} = {{ {g["cell"][0]}, {g["cell"][1]}, '
                     f'{g["anchor"][0]}, {g["anchor"][1]}, {g["count"]} }};')
    lines += ['', '// Unit sheets, named after their data/spr_*.bin']
    for filename, g in units.items():
        if filename not in png_to_bin:
            sys.exit(f"{filename} is in units.json but not in UNIT_SHEETS")
        name = png_to_bin[filename][len('spr_'):]
        lines.append(f'static const SheetGeom GEOM_{name} = {{ {g["cell"][0]}, {g["cell"][1]}, '
                     f'{g["anchor"][0]}, {g["anchor"][1]}, {g["cols"]}, {g["fpd"]}, '
                     f'{"true" if g["once"] else "false"} }};')
    with open(GEOM_HEADER, 'w') as f:
        f.write('\n'.join(lines) + '\n')
    print(f"  {GEOM_HEADER}")


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

    building_geom = []  # (w, h, ax, ay) per BuildingTypeId
    for name in BUILDING_SPRITES:
        if name is None:
            building_geom.append((0, 0, 0, 0))
            continue
        data = load_rgba(name + '.png')
        if data is None:
            sys.exit(f"{name}.png missing: run make assets-game")
        if name in PREALIGNED:
            (cw, ch), (ax, ay) = PREALIGNED[name]
            if (data.shape[1], data.shape[0]) != (cw, ch):
                sys.exit(f"{name}.png is {data.shape[1]}x{data.shape[0]}, expected {cw}x{ch}")
        else:
            layers = []
            shadow = load_rgba(name + '_shadow.png', required=False)
            if shadow is not None:
                layers.append((shadow, *load_hotspot(name + '_shadow')))
            layers.append((data, *load_hotspot(name)))
            data, ax, ay = reduce_layers(layers)
        building_data[name] = data
        building_geom.append((data.shape[1], data.shape[0], int(ax), int(ay)))
        all_images.append(data)
        print(f"  {name}: {data.shape[1]}x{data.shape[0]}, anchor ({ax},{ay})")

    construction_data = {}
    construction_geom = []
    for size in CONSTRUCTION_SIZES:
        stages = []
        for frame in range(CONSTRUCTION_STAGES):
            name = f'construction_{size}_{frame}'
            data = load_rgba(name + '.png')
            if data is None:
                sys.exit(f"{name}.png missing: run make assets-game")
            hx, hy = load_hotspot(name)
            stages.append(reduce_exact(data, hx, hy))
        # One canvas that holds every stage with the hotspot on the same pixel
        left = max(ax for _, ax, _ in stages)
        up = max(ay for _, _, ay in stages)
        w = left + max(d.shape[1] - ax for d, ax, _ in stages)
        h = up + max(d.shape[0] - ay for d, _, ay in stages)
        sheet = np.zeros((h * CONSTRUCTION_STAGES, w, 4), dtype=np.uint8)
        for i, (d, ax, ay) in enumerate(stages):
            y, x = i * h + up - ay, left - ax
            sheet[y:y + d.shape[0], x:x + d.shape[1]] = d
        construction_data[size] = sheet
        construction_geom.append((int(w), int(h), int(left), int(up)))
        all_images.append(sheet)
        print(f"  construction {size}x{size}: {w}x{h} x{CONSTRUCTION_STAGES}, anchor ({left},{up})")

    icon_data = {}
    for name, filename, tw, th, cols, rows, scale in ICON_SPRITES:
        data = load_rgba(filename)
        if data is None:
            continue
        icon_data[name] = (data, tw, th)
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]} -> {tw}x{th}")

    res_sheet_data = {}
    for name in RESOURCE_SHEETS:
        data = load_rgba(f'res_{name}.png')
        if data is None:
            sys.exit(f"res_{name}.png missing: run make assets-game")
        res_sheet_data[name] = data
        all_images.append(data)
        print(f"  res_{name}.png: {data.shape[1]}x{data.shape[0]}")

    resource_data = {}
    for name, filename in RESOURCE_SPRITES:
        data = load_rgba(filename)
        if data is None:
            continue
        resource_data[name] = data
        all_images.append(data)
        print(f"  {filename}: {data.shape[1]}x{data.shape[0]}")

    if not all_images:
        print("ERROR: No sprite images found!")
        sys.exit(1)

    # --- Palette ---
    print("\nBuilding palette from the game's...")
    palette = build_palette(all_images)

    # --- Save NDS palette (BGR555, 256 entries x 2 bytes = 512 bytes) ---
    pal_bin = bytearray(512)
    # Indices 0-15 reserved (0=transparent, 1-15=UI colors set at runtime)
    for i, (r, g, b) in enumerate(palette):
        struct.pack_into('<H', pal_bin, (i + 16) * 2, rgb_to_bgr555(r, g, b))
    pal_path = os.path.join(DATA_DIR, 'sprite_pal.bin')
    with open(pal_path, 'wb') as f:
        f.write(pal_bin)
    print(f"  {pal_path}: {len(pal_bin)} bytes")

    # --- Player colour remap: the game's player 1 ramp onto player 2's ---
    remap = bytearray(range(256))
    for i in range(8):
        remap[16 + i] = 32 + i
    remap_path = os.path.join(DATA_DIR, 'sprite_remap.bin')
    with open(remap_path, 'wb') as f:
        f.write(remap)
    print(f"  {remap_path}: {len(remap)} bytes")

    # Sprite pixels are exact palette colours, so nearest-colour lookup finds
    # them at distance 0; icons and other hand-made art get the closest entry.
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
    for name, data in building_data.items():
        indexed, w, h = index_rgba_image(data, palette_array, prealigned=True)
        out_path = os.path.join(DATA_DIR, f'spr_{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  spr_{name}: {w}x{h} = {len(indexed)} bytes")

    for size, sheet in construction_data.items():
        indexed, w, h = index_rgba_image(sheet, palette_array)
        out_path = os.path.join(DATA_DIR, f'spr_construction_{size}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  spr_construction_{size}: {w}x{h} = {len(indexed)} bytes")

    for name, data in res_sheet_data.items():
        indexed, w, h = index_rgba_image(data, palette_array, prealigned=True)
        out_path = os.path.join(DATA_DIR, f'spr_res_{name}.bin')
        with open(out_path, 'wb') as f:
            f.write(indexed)
        print(f"  spr_res_{name}: {w}x{h} = {len(indexed)} bytes")

    write_geom_header(building_geom, construction_geom)

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
