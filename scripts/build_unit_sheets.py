#!/usr/bin/env python3
"""Build every unit sprite sheet in sprites/ from the AoE2 HD SLPs.

One table, one layout rule, one scale rule — this replaces the mix of
manifest entries and one-off pack_frames.py runs that left sheets with
different shapes (standing sheets holding one direction, or 5 frames total)
and the Archer wearing the Longbowman's graphics.

SLP IDs are the British civ's graphics from empires2_x2_p1.dat, resolved BY
ID with scripts/dat_by_id.js (name in the comment column). An SLP holds five
directions (S, SW, W, NW, N), each with the same number of frames.

Sheet layouts (32x32 cells, one row per direction unless noted):
  stand   5 cols x 5 rows   5 frames per direction
  stand1  5 cols x 1 row    1 frame per direction (ram, mangonel: no idle
                            animation; UNIT_STAND_STATIC in render.cpp)
  anim   10 cols x 5 rows  10 frames per direction
  anim5   5 cols x 5 rows   5 frames per direction (ram, mangonel, monk, and
                            every death sheet)
  carry  10 cols x 3 rows   5 frames per direction, packed back to back
The ARM9 binary has to fit in 3.5 MB together with the sound bank, and these
sheets are most of it — that is why deaths and siege idles are trimmed.
A direction with fewer frames than the layout wants repeats frames; one with
more is sampled evenly. `once` animations (deaths) always end on the last
frame.

Scale: every unit uses UNIT_SCALE unless its standing/walking frames would
not fit the cell, in which case the whole group (all sheets of that unit)
shrinks together. The SLP hotspot — the unit's ground position — lands on
CELL_ANCHOR, which render.cpp puts on the unit's tile.

Usage:
    python3 scripts/build_unit_sheets.py [name ...]
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

from shared_constants import UNIT_CELL, UNIT_ANCHOR, UNIT_SCALE

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics'
SPRITES_DIR = os.path.join(ROOT, 'sprites')

DIRS = 5

# layout -> (columns, frames per direction)
LAYOUTS = {'stand': (5, 5), 'stand1': (5, 1), 'anim': (10, 10), 'anim5': (5, 5), 'carry': (10, 5)}

# group -> [(output png, SLP, layout, once)]; the first two sheets of a group
# (stand, walk) decide its scale.
UNITS = {
    'villager': [
        ('villager.png',          1479, 'stand', False),  # VMBAS_FN
        ('villager_walk.png',     1484, 'anim',  False),  # VMBAS_WN
        ('villager_attack.png',   1473, 'anim',  False),  # VMBAS_AN
        ('villager_carry.png',    1519, 'carry', False),  # VMHUN_CN (meat)
        ('villager_die.png',      1476, 'anim5', True),   # VMBAS_DN
    ],
    'lumberjack': [
        ('lumberjack.png',        1542, 'stand', False),  # VMLUM_FN
        ('lumberjack_walk.png',   1548, 'anim',  False),  # VMLUM_WN
        ('lumberjack_work.png',   1535, 'anim',  False),  # VMLUM_AN (chop)
        ('lumberjack_carry.png',  1536, 'carry', False),  # VMLUM_CN
    ],
    'miner': [
        ('miner.png',             1558, 'stand', False),  # VMMIN_FN
        ('miner_walk.png',        1563, 'anim',  False),  # VMMIN_WN
        ('miner_work.png',        1560, 'anim',  False),  # VMMIN_TN
        ('miner_carry.png',       1552, 'carry', False),  # VMMIN_CN
    ],
    'builder': [
        ('builder.png',           1493, 'stand', False),  # VMBLD_FN
        ('builder_walk.png',      1499, 'anim',  False),  # VMBLD_WN
        ('builder_work.png',      1496, 'anim',  False),  # VMBLD_TN
    ],
    'farmer': [
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
        ('militia_die.png',        990, 'anim5', True),   # SPRMN_DN
    ],
    'archer': [
        ('archer.png',               8, 'stand', False),  # ARCHR_FN
        ('archer_walk.png',         12, 'anim',  False),  # ARCHR_WN
        ('archer_fire.png',          2, 'anim',  False),  # ARCHR_AN
        ('archer_die.png',           5, 'anim5', True),   # ARCHR_DN
    ],
    'knight': [
        ('knight.png',             669, 'stand', False),  # KNGHT_FN
        ('knight_walk.png',        673, 'anim',  False),  # KNGHT_WN
        ('knight_fight.png',       663, 'anim',  False),  # KNGHT_AN
        ('knight_die.png',         666, 'anim5', True),   # KNGHT_DN
    ],
    'spearman': [
        ('spearman.png',           873, 'stand', False),  # PKEMN_FN
        ('spearman_walk.png',      877, 'anim',  False),  # PKEMN_WN
        ('spearman_fight.png',     867, 'anim',  False),  # PKEMN_AN
        ('spearman_die.png',       870, 'anim5', True),   # PKEMN_DN
    ],
    'scout': [
        ('scout.png',             2085, 'stand', False),  # SCOUT_FN
        ('scout_walk.png',        2089, 'anim',  False),  # SCOUT_WN
        ('scout_fight.png',       2079, 'anim',  False),  # SCOUT_AN
        ('scout_die.png',         2082, 'anim5', True),   # SCOUT_DN
    ],
    'sheep': [
        ('sheep_stand.png',       3629, 'stand', False),  # SHEEP_FN
        ('sheep_walk.png',        3634, 'anim',  False),  # SHEEP_WN
        ('sheep_die.png',         3626, 'anim5', True),   # SHEEP_DN
    ],
    'ram': [
        ('ram_stand.png',          179, 'stand1',False),  # BTRAM_FN
        ('ram_walk.png',           183, 'anim5', False),  # BTRAM_WN
        ('ram_fight.png',          173, 'anim5', False),  # BTRAM_AN
        ('ram_death.png',          176, 'anim5', True),   # BTRAM_DN
    ],
    'mangonel': [
        ('mango_stand.png',        722, 'stand1',False),  # MANGO_FN
        ('mango_walk.png',         726, 'anim5', False),  # MANGO_WN
        ('mango_fight.png',        716, 'anim5', False),  # MANGO_AN
        ('mango_death.png',        719, 'anim5', True),   # MANGO_DN
    ],
    'monk': [
        ('monk_stand.png',         774, 'stand', False),  # MONKX_FN
        ('monk_walk.png',          779, 'anim5', False),  # MONKX_WN
        ('monk_fight.png',         768, 'anim5', False),  # MONKX_AN
        ('monk_death.png',         771, 'anim5', True),   # MONKX_DN
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
    """Sample `want` frames for each of the five directions."""
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
            out.append(frames[d * per_dir + k])
    return out


def group_scale(sheets):
    """UNIT_SCALE, or less if the stand/walk frames would overflow the cell."""
    cw, ch = UNIT_CELL
    ax, ay = UNIT_ANCHOR
    left = right = up = 1
    for _, slp, _, _ in sheets[:2]:
        for img, hx, hy in load_slp(slp):
            left, right, up = max(left, hx), max(right, img.width - hx), max(up, hy)
    return min(UNIT_SCALE, ax / left, (cw - ax) / right, ay / up)


def build_sheet(out_name, slp, layout, once, scale):
    cw, ch = UNIT_CELL
    ax, ay = UNIT_ANCHOR
    cols, want = LAYOUTS[layout]
    picked = pick_frames(load_slp(slp), want, once)
    rows = -(-len(picked) // cols)
    sheet = Image.new('RGBA', (cols * cw, rows * ch), (0, 0, 0, 0))
    for i, (img, hx, hy) in enumerate(picked):
        nw = max(1, round(img.width * scale))
        nh = max(1, round(img.height * scale))
        # Premultiplied alpha so edges don't blend toward black
        small = img.convert('RGBa').resize((nw, nh), Image.LANCZOS).convert('RGBA')
        cell = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
        cell.paste(small, (ax - round(hx * scale), ay - round(hy * scale)))  # clips to the cell
        sheet.paste(cell, ((i % cols) * cw, (i // cols) * ch))
    sheet.save(os.path.join(SPRITES_DIR, out_name))
    return sheet.size


def main():
    only = set(sys.argv[1:])
    try:
        for group, sheets in UNITS.items():
            if only and group not in only:
                continue
            scale = group_scale(sheets)
            print(f'{group}: scale {scale:.3f}')
            for out_name, slp, layout, once in sheets:
                w, h = build_sheet(out_name, slp, layout, once, scale)
                print(f'  {out_name}: SLP {slp} {layout} {w}x{h}')
    finally:
        for d in _tmp_dirs:
            shutil.rmtree(d, ignore_errors=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
