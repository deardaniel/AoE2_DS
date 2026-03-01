#!/usr/bin/env python3
import argparse
import math
import os
import re
import sys
from PIL import Image

FRAME_RE = re.compile(r'(\d+)')

def numeric_key(name: str):
    m = FRAME_RE.search(name)
    return int(m.group(1)) if m else name


def load_frames(frames_dir: str):
    files = [f for f in os.listdir(frames_dir) if f.lower().endswith('.png')]
    files.sort(key=numeric_key)
    paths = [os.path.join(frames_dir, f) for f in files]
    return paths


def main():
    ap = argparse.ArgumentParser(description='Pack frame PNGs into a sprite sheet.')
    ap.add_argument('frames_dir', help='Directory containing frame_*.png files')
    ap.add_argument('out_png', help='Output sprite sheet PNG path')
    ap.add_argument('--cell', default='32x32', help='Cell size WxH (default: 32x32)')
    ap.add_argument('--cols', type=int, default=0, help='Columns in sheet (default: auto)')
    ap.add_argument('--limit', type=int, default=0, help='Limit number of frames packed')
    ap.add_argument('--fit', action='store_true', help='Scale frames down to fit cell')
    ap.add_argument('--dirs', type=int, default=0,
                    help='Number of directions in SLP (e.g. 5). '
                         'Samples 5 evenly-spaced frames per direction.')
    # Frames are centered in their cell by default.
    args = ap.parse_args()

    try:
        cell_w, cell_h = [int(x) for x in args.cell.lower().split('x')]
    except Exception:
        print('Invalid --cell format, expected WxH like 32x32', file=sys.stderr)
        return 2

    paths = load_frames(args.frames_dir)
    if not paths:
        print(f'No PNG frames found in {args.frames_dir}', file=sys.stderr)
        return 2

    if args.dirs and args.dirs > 0:
        # Multi-directional SLP: sample 5 frames per direction
        total = len(paths)
        fpd = total // args.dirs  # frames per direction
        if fpd < 5:
            print(f'Not enough frames per direction: {fpd} (need >= 5)', file=sys.stderr)
            return 2
        sampled = []
        for d in range(args.dirs):
            base = d * fpd
            # Pick 5 evenly spaced across the walk/attack cycle
            for i in range(5):
                idx = base + i * fpd // 5
                if idx < total:
                    sampled.append(paths[idx])
        paths = sampled
        print(f'Sampled {len(paths)} frames from {total} ({args.dirs} dirs × 5 anim)')
    elif args.limit and args.limit > 0:
        paths = paths[: args.limit]

    n = len(paths)
    cols = args.cols if args.cols > 0 else int(math.ceil(math.sqrt(n)))
    rows = int(math.ceil(n / cols))

    sheet_w = cols * cell_w
    sheet_h = rows * cell_h
    sheet = Image.new('RGBA', (sheet_w, sheet_h), (0, 0, 0, 0))

    for i, path in enumerate(paths):
        img = Image.open(path).convert('RGBA')
        if args.fit:
            img.thumbnail((cell_w, cell_h), Image.NEAREST)
        if img.width > cell_w or img.height > cell_h:
            print(
                f'Frame too large for cell: {os.path.basename(path)} '
                f'({img.width}x{img.height}) > {cell_w}x{cell_h}. '
                f'Use --fit or a larger --cell.',
                file=sys.stderr,
            )
            return 2

        col = i % cols
        row = i // cols
        x = col * cell_w
        y = row * cell_h
        x += (cell_w - img.width) // 2
        y += (cell_h - img.height) // 2
        sheet.alpha_composite(img, (x, y))

    os.makedirs(os.path.dirname(args.out_png) or '.', exist_ok=True)
    sheet.save(args.out_png)
    print(f'Saved sprite sheet: {args.out_png} ({sheet_w}x{sheet_h}, {n} frames)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
