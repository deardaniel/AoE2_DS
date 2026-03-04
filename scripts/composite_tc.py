#!/usr/bin/env python3
"""Composite Town Center from multiple AoE2 SLP layers.

The Dark Age TC (RTWC, unit 99) is composed of 9 layers from the
RTWC1X preview graphic (Graphic 3345). Each layer references a
GraphicDelta with a sub-graphic ID and Y offset. The compositing
uses hotspot alignment + delta Y offsets:

  drawX = -hotspotX + deltaOffsetX
  drawY = -hotspotY + deltaOffsetY

Layer order from .dat file (RTWC1X graphic 3345 deltas):
  0: SLP 890  (foundation/pillars)    - MISSING, skip
  1: SLP 889  (shadow/base)           offset Y=0
  2: SLP 891  (center building)       offset Y=-48
  3: SLP 3596 (right wing roof)       offset Y=0
  4: SLP 4641 (right wing detail)     offset Y=0
  5: SLP 3595 (right wing supports)   offset Y=+24
  6: SLP 4640 (right wing roof top)   offset Y=+24
  7: SLP 3594 (left wing)             offset Y=+48
  8: SLP 4639 (left wing roof top)    offset Y=+48

Usage:
    python3 scripts/composite_tc.py [--target WxH]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics'
PALETTE = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/interface/50500.bina'

# Layer order: back to front, from RTWC1X graphic 3345 deltas
# (slp_id, delta_offset_x, delta_offset_y, description)
TC_LAYERS = [
    # SLP 889 (layer 5) is a selection/damage outline — skip for normal rendering
    # SLP 890 (layer 10) is foundation — file missing, skip
    # Base layers are all Generic (G suffix) — shared across all architectures
    (891,  0, -48, 'center building (G)'),
    (3596, 0,   0, 'right wing pillars (G)'),
    (4641, 0,   0, 'wing columns (E — small, barely visible)'),
    (3595, 0,  24, 'right wing single pillar (G)'),
    (4639, 0,  24, 'right wing canopy (M — matches left wing style)'),
    (3594, 0,  48, 'left wing roof (G)'),
    (4639, 0,  48, 'left wing canopy (M — user confirmed correct)'),
]


def extract_slp(slp_id, out_dir):
    slp_path = os.path.join(SLP_DIR, f'{slp_id}.slp')
    if not os.path.exists(slp_path):
        print(f'SLP not found: {slp_path}', file=sys.stderr)
        return None, None
    cmd = ['node', 'extract-slp.js', slp_path, out_dir, PALETTE]
    subprocess.run(cmd, check=True, capture_output=True)
    img = Image.open(os.path.join(out_dir, 'frame_0.png')).convert('RGBA')
    meta_path = os.path.join(out_dir, 'frame_0.json')
    if os.path.exists(meta_path):
        with open(meta_path) as f:
            meta = json.load(f)
        return img, (meta['hotspotX'], meta['hotspotY'])
    return img, (0, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--target', default='128x96', help='Target WxH (default: 128x96)')
    ap.add_argument('--out', default='sprites/town_center.png', help='Output path')
    args = ap.parse_args()

    tw, th = [int(x) for x in args.target.split('x')]

    layers = []
    tmp_dirs = []
    try:
        for slp_id, dx, dy, name in TC_LAYERS:
            tmp = tempfile.mkdtemp(prefix=f'tc_{slp_id}_')
            tmp_dirs.append(tmp)
            img, hotspot = extract_slp(slp_id, tmp)
            if img is None:
                print(f'  Skipping SLP {slp_id} ({name})')
                continue
            print(f'  SLP {slp_id} ({name}): {img.size}, hotspot {hotspot}, delta ({dx},{dy})')
            layers.append((img, hotspot, dx, dy))
    finally:
        for d in tmp_dirs:
            shutil.rmtree(d, ignore_errors=True)

    if not layers:
        print('No layers extracted!', file=sys.stderr)
        return 1

    # Calculate draw position for each layer:
    # drawX = deltaOffsetX - hotspotX
    # drawY = deltaOffsetY - hotspotY
    draw_positions = []
    for img, (hx, hy), dx, dy in layers:
        draw_x = dx - hx
        draw_y = dy - hy
        draw_positions.append((draw_x, draw_y, img))
        print(f'    draw at ({draw_x}, {draw_y}), size {img.size}')

    # Calculate bounding box of all layers
    min_x = min(x for x, y, img in draw_positions)
    max_x = max(x + img.width for x, y, img in draw_positions)
    min_y = min(y for x, y, img in draw_positions)
    max_y = max(y + img.height for x, y, img in draw_positions)
    cw, ch = max_x - min_x, max_y - min_y
    print(f'  Canvas: {cw}x{ch} (min {min_x},{min_y} max {max_x},{max_y})')

    # Composite all layers
    canvas = Image.new('RGBA', (cw, ch), (0, 0, 0, 0))
    for draw_x, draw_y, img in draw_positions:
        paste_x = draw_x - min_x
        paste_y = draw_y - min_y
        canvas.alpha_composite(img, (paste_x, paste_y))

    # Crop to content
    bbox = canvas.getbbox()
    if bbox:
        canvas = canvas.crop(bbox)
    print(f'  Cropped: {canvas.size}')

    # Scale to target, bottom-anchored
    scale = min(tw / canvas.width, th / canvas.height)
    nw, nh = int(canvas.width * scale), int(canvas.height * scale)
    resized = canvas.resize((nw, nh), Image.LANCZOS)

    final = Image.new('RGBA', (tw, th), (0, 0, 0, 0))
    final.paste(resized, ((tw - nw) // 2, th - nh), resized)
    final.save(args.out)
    print(f'Saved {args.out}: {tw}x{th} (content {nw}x{nh})')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
