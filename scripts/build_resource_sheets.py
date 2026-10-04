#!/usr/bin/env python3
"""Build the resource sprite sheets (trees, mines, berry bushes).

In the game these are Gaia units whose standing graphic holds several
variants — 14 oaks, 7 gold piles, 7 stone piles, 4 forage bushes — and each
tile shows one of them. Trees and mines also have an under graphic (the delta
of the standing graphic in the .dat) with a matching frame per variant: the
shadow, and for gold the rock the nuggets sit on.

Each variant is reduced by exactly 3 around its hotspot (the centre of its
tile) with its shadow underneath, and the variants are packed side by side in
sprites/res_<name>.png. Shadow pixels are black at SHADOW_ALPHA, which
preprocess_sprites.py turns into the marker the renderer darkens terrain
with. sprites/resources.json records cell, anchor and variant count.

Usage:
    python3 scripts/build_resource_sheets.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

from downscale import reduce_layers

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics'
SPRITES_DIR = os.path.join(ROOT, 'sprites')

# name -> (Gaia unit ID, SLP, shadow SLP or None, graphic names)
RESOURCES = {
    'tree':    (349, 4652, 2296, 'FOAK_NN + FOAK_N0'),     # oak forest
    'gold':    (66,  2561, 4479, 'GOLDM_NN + GOLDM_N0'),
    'stone':   (102, 1034, 4482, 'STONM_NN + STONM_N0'),
    'berries': (59,  2560, None, 'FORAG_NN'),
}


def load_slp(slp, tmp_dirs):
    slp_path = os.path.join(SLP_DIR, f'{slp}.slp')
    if not os.path.exists(slp_path):
        sys.exit(f'SLP not found: {slp_path}')
    tmp = tempfile.mkdtemp(prefix=f'res_{slp}_')
    tmp_dirs.append(tmp)
    subprocess.run(['node', os.path.join(ROOT, 'extract-slp.js'), slp_path, tmp],
                   check=True, capture_output=True, cwd=ROOT)
    frames = []
    while os.path.exists(os.path.join(tmp, f'frame_{len(frames)}.png')):
        i = len(frames)
        with open(os.path.join(tmp, f'frame_{i}.json')) as f:
            meta = json.load(f)
        img = np.array(Image.open(os.path.join(tmp, f'frame_{i}.png')).convert('RGBA'))
        frames.append((img, meta['hotspotX'], meta['hotspotY']))
    return frames


def main():
    geom = {}
    tmp_dirs = []
    try:
        for name, (unit, slp, shadow_slp, gname) in RESOURCES.items():
            frames = load_slp(slp, tmp_dirs)
            shadows = load_slp(shadow_slp, tmp_dirs) if shadow_slp else None
            variants = []
            for i, frame in enumerate(frames):
                # Under graphic first: its shadow, and for gold the rock the
                # nuggets sit on (GOLDM_NN is only the nuggets)
                layers = ([shadows[i]] if shadows else []) + [frame]
                variants.append(reduce_layers(layers))
            left = int(max(ax for _, ax, _ in variants))
            up = int(max(ay for _, _, ay in variants))
            cw = left + int(max(v.shape[1] - ax for v, ax, _ in variants))
            ch = up + int(max(v.shape[0] - ay for v, _, ay in variants))
            sheet = np.zeros((ch, cw * len(variants), 4), dtype=np.uint8)
            for i, (v, ax, ay) in enumerate(variants):
                x, y = i * cw + left - ax, up - ay
                sheet[y:y + v.shape[0], x:x + v.shape[1]] = v
            Image.fromarray(sheet).save(os.path.join(SPRITES_DIR, f'res_{name}.png'))
            geom[name] = {'cell': [cw, ch], 'anchor': [left, up], 'count': len(variants)}
            print(f'  res_{name}.png: SLP {slp} ({gname}), {len(variants)} variants, '
                  f'cell {cw}x{ch}, anchor ({left},{up})')
    finally:
        for d in tmp_dirs:
            shutil.rmtree(d, ignore_errors=True)
    with open(os.path.join(SPRITES_DIR, 'resources.json'), 'w') as f:
        json.dump(geom, f, indent=1)
        f.write('\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
