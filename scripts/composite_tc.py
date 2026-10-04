#!/usr/bin/env python3
"""Composite the Dark Age Town Center from its AoE2 SLP layers.

The TC (unit 109 "RTWC") is not one sprite: the game builds it from a main
unit plus three annex units, each with its own graphic. The head unit 621
"RTWC1X" carries graphic 3345 (RTWC1CNG), whose deltas list every piece with
its screen offset — that list is the layout used here (dumped from
empires2_x2_p1.dat, see scripts/dump_tc_graphics.js):

  SLP 890  (0,  0)  foundation        — file not shipped with HD, skipped
  SLP 889  (0,  0)  ground shadow     — SLP shadow commands only
  SLP 891  (0,-48)  centre building   (annex 618 at tile offset +1,-1)
  SLP 3596 (0,  0)  left wing posts   (main unit)
  SLP 4612 (0,  0)  right wing posts
  SLP 3595 (0, 24)  left far post     (annex 619 at -0.5,+0.5)
  SLP 4611 (0, 24)  right far post
  SLP 3594 (0, 48)  left wing roof    (annex 620 at -1,+1)
  SLP 4610 (0, 48)  right wing roof

All nine are the generic Dark Age set (RTWC1N?G). genie-dat's graphics and
objects arrays are NOT indexed by ID — look entries up by their .id field, or
the right-wing deltas resolve to Imperial Age pieces from other civ sets.

Every layer is placed at (delta - hotspot), so the origin of the composite is
the unit position: the CENTRE of the 4x4 footprint diamond. AoE2 tiles are
96x48 and ours are 32x16, so the composite is reduced by exactly 3 with the
origin kept on a pixel boundary. No fitting, cropping or ratio guessing: the
output canvas is final and render.cpp places it by TC_ANCHOR.

Output pixels are either fully opaque (building), fully transparent, or
black with SHADOW_ALPHA (ground shadow, which preprocess_sprites.py turns
into the shadow marker index that the renderer darkens terrain with).

Usage:
    python3 scripts/composite_tc.py [--out sprites/town_center.png] [--debug DIR]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

from downscale import reduce_exact, SCALE
from shared_constants import TC_CANVAS, TC_ANCHOR, SHADOW_ALPHA

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
AOE2 = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs'
SLP_DIR = os.path.join(AOE2, 'graphics')
PALETTE = os.path.join(AOE2, 'interface', '50500.bina')


# (slp_id, delta_x, delta_y, description), back to front
TC_LAYERS = [
    (889,  0,   0, 'ground shadow'),
    (891,  0, -48, 'centre building'),
    (3596, 0,   0, 'left wing posts'),
    (4612, 0,   0, 'right wing posts'),
    (3595, 0,  24, 'left far post'),
    (4611, 0,  24, 'right far post'),
    (3594, 0,  48, 'left wing roof'),
    (4610, 0,  48, 'right wing roof'),
]


def extract_slp(slp_id, out_dir):
    """Return (RGBA image, (hotspot_x, hotspot_y)) for frame 0 of an SLP."""
    slp_path = os.path.join(SLP_DIR, f'{slp_id}.slp')
    if not os.path.exists(slp_path):
        sys.exit(f'SLP not found: {slp_path}')
    subprocess.run(['node', os.path.join(ROOT, 'extract-slp.js'), slp_path, out_dir, PALETTE],
                   check=True, capture_output=True, cwd=ROOT)
    img = Image.open(os.path.join(out_dir, 'frame_0.png')).convert('RGBA')
    with open(os.path.join(out_dir, 'frame_0.json')) as f:
        meta = json.load(f)
    return img, (meta['hotspotX'], meta['hotspotY'])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(ROOT, 'sprites', 'town_center.png'))
    ap.add_argument('--debug', help='directory for a full-resolution composite')
    args = ap.parse_args()

    cw, ch = TC_CANVAS
    ax, ay = TC_ANCHOR
    # Full-resolution working canvas: the output canvas times SCALE, so the
    # hotspot sits at (ax, ay) * SCALE and every 3x3 block maps to one pixel.
    fw, fh = cw * SCALE, ch * SCALE
    ox, oy = ax * SCALE, ay * SCALE

    color = np.zeros((fh, fw, 3), dtype=np.float64)   # building RGB
    solid = np.zeros((fh, fw), dtype=np.float64)      # building coverage
    shadow = np.zeros((fh, fw), dtype=np.float64)     # shadow coverage

    tmp_dirs = []
    try:
        for slp_id, dx, dy, name in TC_LAYERS:
            tmp = tempfile.mkdtemp(prefix=f'tc_{slp_id}_')
            tmp_dirs.append(tmp)
            img, (hx, hy) = extract_slp(slp_id, tmp)
            x0, y0 = ox + dx - hx, oy + dy - hy
            x1, y1 = x0 + img.width, y0 + img.height
            print(f'  SLP {slp_id} ({name}): {img.width}x{img.height} '
                  f'hotspot ({hx},{hy}) -> ({x0 - ox},{y0 - oy})')
            if x0 < 0 or y0 < 0 or x1 > fw or y1 > fh:
                sys.exit(f'SLP {slp_id} falls outside TC_CANVAS {TC_CANVAS} '
                         f'with TC_ANCHOR {TC_ANCHOR}; enlarge it in shared_constants.py '
                         f'and render.cpp')

            px = np.asarray(img, dtype=np.float64)
            opaque = px[:, :, 3] > 0
            # genie-slp renders SLP shadow commands as pure red
            is_shadow = opaque & (px[:, :, 0] == 255) & (px[:, :, 1] == 0) & (px[:, :, 2] == 0)
            is_solid = opaque & ~is_shadow

            region = (slice(y0, y1), slice(x0, x1))
            shadow[region][is_shadow] = 1.0
            color[region][is_solid] = px[:, :, :3][is_solid]
            solid[region][is_solid] = 1.0
    finally:
        for d in tmp_dirs:
            shutil.rmtree(d, ignore_errors=True)

    if args.debug:
        os.makedirs(args.debug, exist_ok=True)
        dbg = np.zeros((fh, fw, 4), dtype=np.uint8)
        dbg[shadow > 0] = (0, 0, 0, SHADOW_ALPHA)
        dbg[solid > 0, :3] = color[solid > 0]
        dbg[solid > 0, 3] = 255
        Image.fromarray(dbg).save(os.path.join(args.debug, 'town_center_full.png'))

    # Reduce by SCALE. Building pixels are picked, not blended (see
    # downscale.py); the full canvas is already block-aligned on the anchor.
    full = np.zeros((fh, fw, 4), dtype=np.uint8)
    full[solid > 0, :3] = color[solid > 0]
    full[solid > 0, 3] = 255
    small, sx, sy = reduce_exact(full, ox, oy)
    out = np.zeros((ch, cw, 4), dtype=np.uint8)
    x0, y0 = ax - sx, ay - sy
    out[y0:y0 + small.shape[0], x0:x0 + small.shape[1]] = small

    # Shadow where most of a block is shadow and the building doesn't cover it
    shade = (shadow * (1 - solid)).reshape(ch, SCALE, cw, SCALE).sum(axis=(1, 3))
    is_shadow = (out[:, :, 3] == 0) & (shade * 2 > SCALE * SCALE)
    out[is_shadow] = (0, 0, 0, SHADOW_ALPHA)

    Image.fromarray(out).save(args.out)
    ys, xs = np.nonzero(out[:, :, 3])
    print(f'Saved {args.out}: {cw}x{ch}, anchor {TC_ANCHOR}, '
          f'content x {xs.min()}..{xs.max()} y {ys.min()}..{ys.max()}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
