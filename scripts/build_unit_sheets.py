#!/usr/bin/env python3
"""Build every unit sprite sheet in sprites/ from the AoE2 HD SLPs.

One table, one layout rule, one scale. SLP IDs are the British civ's graphics
from empires2_x2_p1.dat, resolved BY ID with scripts/dat_by_id.js (name in the
comment column). An SLP holds five directions (S, SW, W, NW, N), each with
the same number of frames.

Sheet layouts (one row per direction unless noted):
  stand   5 cols x 5 rows   5 frames per direction
  stand1  5 cols x 1 row    1 frame per direction (ram, mangonel: the game
                            has no idle animation for them)
  anim   10 cols x 5 rows  10 frames per direction
  anim5   5 cols x 5 rows   5 frames per direction (unused; halves a sheet)
  carry  10 cols x 3 rows   5 frames per direction, packed back to back
A direction with fewer frames than the layout wants repeats frames; one with
more is sampled evenly. `once` animations (deaths) always end on the last
frame.

Every frame is reduced by exactly 3 (scripts/downscale.py). Each sheet gets
its own cell size: the smallest cell that holds every frame of that sheet
with the SLP hotspot (the unit's ground position) on one fixed anchor pixel.
sprites/units.json records cell, anchor and layout per sheet;
preprocess_sprites.py turns it into source/sprite_geom.h.

The ARM9 image has to fit a 2.6 MB region together with the sound bank; with
per-sheet cells the sheets total about 1.3 MB, leaving roughly 0.5 MB free.

Usage:
    python3 scripts/build_unit_sheets.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

from downscale import reduce_exact, best_phase

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics'
SPRITES_DIR = os.path.join(ROOT, 'sprites')

DIRS = 5

# layout -> (columns, frames per direction)
LAYOUTS = {'stand': (5, 5), 'stand1': (5, 1), 'anim': (10, 10), 'anim5': (5, 5), 'carry': (10, 5)}

# unit type (UnitTypeId order in config.h) -> [(output png, SLP, layout, once)]
UNITS = {
    'villager': [
        ('villager.png',          1479, 'stand', False),  # VMBAS_FN
        ('villager_walk.png',     1484, 'anim',  False),  # VMBAS_WN
        ('villager_attack.png',   1473, 'anim',  False),  # VMBAS_AN
        ('villager_carry.png',    1519, 'carry', False),  # VMHUN_CN (meat)
        ('villager_die.png',      1476, 'anim',  True),   # VMBAS_DN
        # lumberjack
        ('lumberjack.png',        1542, 'stand', False),  # VMLUM_FN
        ('lumberjack_walk.png',   1548, 'anim',  False),  # VMLUM_WN
        ('lumberjack_work.png',   1535, 'anim',  False),  # VMLUM_AN (chop)
        ('lumberjack_carry.png',  1536, 'carry', False),  # VMLUM_CN
        # miner
        ('miner.png',             1558, 'stand', False),  # VMMIN_FN
        ('miner_walk.png',        1563, 'anim',  False),  # VMMIN_WN
        ('miner_work.png',        1560, 'anim',  False),  # VMMIN_TN
        ('miner_carry.png',       1552, 'carry', False),  # VMMIN_CN
        # builder
        ('builder.png',           1493, 'stand', False),  # VMBLD_FN
        ('builder_walk.png',      1499, 'anim',  False),  # VMBLD_WN
        ('builder_work.png',      1496, 'anim',  False),  # VMBLD_TN
        # farmer
        ('farmer.png',            1509, 'stand', False),  # VMFAR_FN
        ('farmer_walk.png',       1515, 'anim',  False),  # VMFAR_WN
        ('farmer_work.png',       1512, 'anim',  False),  # VMFAR_TN
        ('farmer_carry.png',      1519, 'carry', False),  # VMHUN_CN (meat)
        ('forager_carry.png',     2592, 'carry', False),  # VMFOR_CN (basket)
    ],
    'militia': [
        ('militia.png',            993, 'stand', False),  # SPRMN_FN
        ('militia_walk.png',       997, 'anim',  False),  # SPRMN_WN
        ('militia_fight.png',      987, 'anim',  False),  # SPRMN_AN
        ('militia_die.png',        990, 'anim',  True),   # SPRMN_DN
    ],
    'archer': [
        ('archer.png',               8, 'stand', False),  # ARCHR_FN
        ('archer_walk.png',         12, 'anim',  False),  # ARCHR_WN
        ('archer_fire.png',          2, 'anim',  False),  # ARCHR_AN
        ('archer_die.png',           5, 'anim',  True),   # ARCHR_DN
    ],
    'knight': [
        ('knight.png',             669, 'stand', False),  # KNGHT_FN
        ('knight_walk.png',        673, 'anim',  False),  # KNGHT_WN
        ('knight_fight.png',       663, 'anim',  False),  # KNGHT_AN
        ('knight_die.png',         666, 'anim',  True),   # KNGHT_DN
    ],
    'spearman': [
        ('spearman.png',           873, 'stand', False),  # PKEMN_FN
        ('spearman_walk.png',      877, 'anim',  False),  # PKEMN_WN
        ('spearman_fight.png',     867, 'anim',  False),  # PKEMN_AN
        ('spearman_die.png',       870, 'anim',  True),   # PKEMN_DN
    ],
    'scout': [
        ('scout.png',             2085, 'stand', False),  # SCOUT_FN
        ('scout_walk.png',        2089, 'anim',  False),  # SCOUT_WN
        ('scout_fight.png',       2079, 'anim',  False),  # SCOUT_AN
        ('scout_die.png',         2082, 'anim',  True),   # SCOUT_DN
    ],
    'sheep': [
        ('sheep_stand.png',       3629, 'stand', False),  # SHEEP_FN
        ('sheep_walk.png',        3634, 'anim',  False),  # SHEEP_WN
        ('sheep_die.png',         3626, 'anim',  True),   # SHEEP_DN
    ],
    'ram': [
        ('ram_stand.png',          179, 'stand1',False),  # BTRAM_FN
        ('ram_walk.png',    (181, 183), 'anim',  False),  # BTRAM_W0 body + BTRAM_WN wheels
        ('ram_fight.png',   (171, 173), 'anim',  False),  # BTRAM_A0 body + BTRAM_AN ram head
        ('ram_death.png',          176, 'anim',  True),   # BTRAM_DN
    ],
    'mangonel': [
        ('mango_stand.png',        722, 'stand1',False),  # MANGO_FN
        ('mango_walk.png',  (724, 726), 'anim',  False),  # MANGO_W0 body + MANGO_WN wheels
        ('mango_fight.png',        716, 'anim',  False),  # MANGO_AN
        ('mango_death.png',        719, 'anim',  True),   # MANGO_DN
    ],
    'monk': [
        ('monk_stand.png',         774, 'stand', False),  # MONKX_FN
        ('monk_walk.png',          779, 'anim',  False),  # MONKX_WN
        ('monk_fight.png',         768, 'anim',  False),  # MONKX_AN
        ('monk_death.png',         771, 'anim',  True),   # MONKX_DN
    ],
}

_slp_cache = {}
_tmp_dirs = []


def load_slp(slp):
    """Return [(RGBA image, hotspot_x, hotspot_y)] for every frame of an SLP."""
    if slp in _slp_cache:
        return _slp_cache[slp]
    slp_path = os.path.join(SLP_DIR, f'{slp}.slp')
    if not os.path.exists(slp_path):
        sys.exit(f'SLP not found: {slp_path}')
    tmp = tempfile.mkdtemp(prefix=f'unit_{slp}_')
    _tmp_dirs.append(tmp)
    subprocess.run(['node', os.path.join(ROOT, 'extract-slp.js'), slp_path, tmp],
                   check=True, capture_output=True, cwd=ROOT)
    frames = []
    i = 0
    while os.path.exists(os.path.join(tmp, f'frame_{i}.png')):
        with open(os.path.join(tmp, f'frame_{i}.json')) as f:
            meta = json.load(f)
        img = Image.open(os.path.join(tmp, f'frame_{i}.png')).convert('RGBA')
        img.load()
        frames.append((img, meta['hotspotX'], meta['hotspotY']))
        i += 1
    if not frames:
        sys.exit(f'SLP {slp}: no frames extracted')
    _slp_cache[slp] = frames
    return frames


def pick_frames(frames, want, once):
    """Sample `want` frames for each of the five directions.

    Returns [(direction, frame)]; frame is (RGBA image, hotspot_x, hotspot_y).
    """
    # A few SLPs are a frame or two short in the last direction, so round the
    # per-direction count up and clamp.
    per_dir = -(-len(frames) // DIRS)
    out = []
    for d in range(DIRS):
        have = min(per_dir, len(frames) - d * per_dir)
        for i in range(want):
            if once:
                k = round(i * (have - 1) / (want - 1)) if want > 1 else 0
            else:
                k = i * have // want
            out.append((d, frames[d * per_dir + k]))
    return out


def layer_under(img, hx, hy, base, bhx, bhy):
    """Draw `img` over `base`, lining up their hotspots."""
    left, up = max(hx, bhx), max(hy, bhy)
    w = left + max(img.width - hx, base.width - bhx)
    h = up + max(img.height - hy, base.height - bhy)
    out = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    out.alpha_composite(base, (left - bhx, up - bhy))
    out.alpha_composite(img, (left - hx, up - hy))
    return out, left, up


def source_frames(slp, layout, once):
    """The full-size frames a sheet uses: [(direction, RGBA array, hx, hy)].

    `slp` is one SLP, or (body, animated): the game draws siege units as a
    static body graphic (one frame per direction) with an animated part on
    top — the delta graphics of e.g. BTRAM_WN in the .dat.
    """
    body, slp = slp if isinstance(slp, tuple) else (None, slp)
    _, want = LAYOUTS[layout]
    out = []
    for d, (img, hx, hy) in pick_frames(load_slp(slp), want, once):
        if body is not None:
            base, bhx, bhy = load_slp(body)[d]
            img, hx, hy = layer_under(img, hx, hy, base, bhx, bhy)
        out.append((d, np.asarray(img), hx, hy))
    return out


def main():
    geom = {}
    total = 0
    try:
        for unit_type, sheets in UNITS.items():
            print(unit_type)
            # One sampling phase per direction for the whole unit, so it
            # doesn't shift when it changes from standing to walking
            sources = [source_frames(slp, layout, once) for _, slp, layout, once in sheets]
            phases = [best_phase([(a, hx, hy) for src in sources for dd, a, hx, hy in src if dd == d])
                      for d in range(DIRS)]
            print(f'  phases {phases}')
            for (out_name, slp, layout, once), src in zip(sheets, sources):
                cols, fpd = LAYOUTS[layout]
                frames = [reduce_exact(a, hx, hy, phase=phases[d]) for d, a, hx, hy in src]
                # Smallest cell holding every frame with the hotspot on one pixel
                left = int(max(ax for _, ax, _ in frames))
                up = int(max(ay for _, _, ay in frames))
                right = int(max(f.shape[1] - ax for f, ax, _ in frames))
                down = int(max(f.shape[0] - ay for f, _, ay in frames))
                cw, ch = left + right, up + down
                rows = -(-len(frames) // cols)
                sheet = np.zeros((rows * ch, cols * cw, 4), dtype=np.uint8)
                for i, (f, ax, ay) in enumerate(frames):
                    x = (i % cols) * cw + left - ax
                    y = (i // cols) * ch + up - ay
                    sheet[y:y + f.shape[0], x:x + f.shape[1]] = f
                Image.fromarray(sheet).save(os.path.join(SPRITES_DIR, out_name))
                geom[out_name] = {'cell': [cw, ch], 'anchor': [left, up],
                                  'cols': cols, 'fpd': fpd, 'once': once}
                total += sheet.shape[0] * sheet.shape[1]
                print(f'  {out_name}: SLP {slp} {layout}, cell {cw}x{ch}, anchor ({left},{up})')
    finally:
        for d in _tmp_dirs:
            shutil.rmtree(d, ignore_errors=True)
    with open(os.path.join(SPRITES_DIR, 'units.json'), 'w') as f:
        json.dump(geom, f, indent=1)
        f.write('\n')
    print(f'{total} bytes of unit sheets')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
