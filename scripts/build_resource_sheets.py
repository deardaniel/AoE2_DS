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

from downscale import reduce_exact, SCALE
from shared_constants import SHADOW_ALPHA

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


def reduce_shadow(img, hx, hy):
    """Reduce a shadow frame to a boolean mask and its anchor.

    genie-slp draws SLP shadow commands as pure red; a block is shadow when
    most of it is.
    """
    h, w = img.shape[:2]
    is_shadow = (img[:, :, 3] > 0) & (img[:, :, 0] == 255) & (img[:, :, 1] == 0) & (img[:, :, 2] == 0)
    bx0, by0 = (0 - hx) // SCALE, (0 - hy) // SCALE
    bx1, by1 = -(-(w - hx) // SCALE), -(-(h - hy) // SCALE)
    padded = np.zeros(((by1 - by0) * SCALE, (bx1 - bx0) * SCALE), dtype=np.int32)
    ox, oy = -hx - bx0 * SCALE, -hy - by0 * SCALE
    padded[oy:oy + h, ox:ox + w] = is_shadow
    cover = padded.reshape(by1 - by0, SCALE, bx1 - bx0, SCALE).sum(axis=(1, 3))
    return cover * 2 > SCALE * SCALE, -bx0, -by0


def main():
    geom = {}
    tmp_dirs = []
    try:
        for name, (unit, slp, shadow_slp, gname) in RESOURCES.items():
            frames = load_slp(slp, tmp_dirs)
            shadows = load_slp(shadow_slp, tmp_dirs) if shadow_slp else None
            variants = []
            for i, (img, hx, hy) in enumerate(frames):
                # Layers bottom to top: shadow, the under graphic's own pixels
                # (the gold pile's rock is there, not in GOLDM_NN), the sprite
                layers = []
                if shadows:
                    simg, shx, shy = shadows[i]
                    mask, sax, say = reduce_shadow(simg, shx, shy)
                    shade = np.zeros(mask.shape + (4,), dtype=np.uint8)
                    shade[mask] = (0, 0, 0, SHADOW_ALPHA)
                    layers.append((shade, sax, say))
                    under = simg.copy()
                    under[(simg[:, :, 0] == 255) & (simg[:, :, 1] == 0) & (simg[:, :, 2] == 0)] = 0
                    if under[:, :, 3].any():
                        layers.append(reduce_exact(under, shx, shy))
                layers.append(reduce_exact(img, hx, hy))

                left = max(ax for _, ax, _ in layers)
                up = max(ay for _, _, ay in layers)
                w = left + max(l.shape[1] - ax for l, ax, _ in layers)
                h = up + max(l.shape[0] - ay for l, _, ay in layers)
                out = np.zeros((h, w, 4), dtype=np.uint8)
                for l, ax, ay in layers:
                    region = out[up - ay:up - ay + l.shape[0], left - ax:left - ax + l.shape[1]]
                    region[l[:, :, 3] > 0] = l[l[:, :, 3] > 0]
                variants.append((out, left, up))
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
