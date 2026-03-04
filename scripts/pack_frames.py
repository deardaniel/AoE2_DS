#!/usr/bin/env python3
import argparse
import json
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


def load_hotspot(png_path: str):
    """Load hotspot metadata from companion .json file if present."""
    json_path = os.path.splitext(png_path)[0] + '.json'
    if os.path.exists(json_path):
        with open(json_path, 'r') as f:
            meta = json.load(f)
        return meta.get('hotspotX'), meta.get('hotspotY')
    return None, None


def main():
    ap = argparse.ArgumentParser(description='Pack frame PNGs into a sprite sheet.')
    ap.add_argument('frames_dir', help='Directory containing frame_*.png files')
    ap.add_argument('out_png', help='Output sprite sheet PNG path')
    ap.add_argument('--cell', default='32x32', help='Cell size WxH (default: 32x32)')
    ap.add_argument('--cols', type=int, default=0, help='Columns in sheet (default: auto)')
    ap.add_argument('--limit', type=int, default=0, help='Limit number of frames packed')
    ap.add_argument('--fit', action='store_true', help='Scale frames down to fit cell')
    ap.add_argument('--scale', type=float, default=0.0,
                    help='Override uniform scale factor (e.g. 0.45). Overrides --fit.')
    ap.add_argument('--dirs', type=int, default=0,
                    help='Number of directions in SLP (e.g. 5). '
                         'Samples --fpd evenly-spaced frames per direction.')
    ap.add_argument('--fpd', type=int, default=10,
                    help='Frames per direction to sample (default: 10)')
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
        # Multi-directional SLP: sample N frames per direction
        sample_count = args.fpd
        total = len(paths)
        fpd = total // args.dirs  # frames per direction in source
        if fpd < sample_count:
            print(f'Not enough frames per direction: {fpd} (need >= {sample_count})', file=sys.stderr)
            return 2
        sampled = []
        for d in range(args.dirs):
            base = d * fpd
            # Pick sample_count evenly spaced across the walk/attack cycle
            for i in range(sample_count):
                idx = base + i * fpd // sample_count
                if idx < total:
                    sampled.append(paths[idx])
        paths = sampled
        print(f'Sampled {len(paths)} frames from {total} ({args.dirs} dirs x {sample_count} anim)')
    elif args.limit and args.limit > 0:
        paths = paths[: args.limit]

    n = len(paths)
    cols = args.cols if args.cols > 0 else int(math.ceil(math.sqrt(n)))
    rows = int(math.ceil(n / cols))

    sheet_w = cols * cell_w
    sheet_h = rows * cell_h
    sheet = Image.new('RGBA', (sheet_w, sheet_h), (0, 0, 0, 0))

    # Hotspot anchor point within the cell: center-X, near-bottom-Y
    # This keeps the unit's feet at a stable position
    anchor_x = cell_w // 2
    anchor_y = cell_h - 2  # 2px from bottom edge

    # Pre-scan all frames to compute a uniform scale factor
    # so all frames stay the same size relative to each other
    uniform_scale = 1.0
    if args.scale > 0:
        uniform_scale = args.scale
        print(f'Using override scale: {uniform_scale:.3f}')
    elif args.fit:
        max_w = 0
        max_h = 0
        for path in paths:
            img = Image.open(path)
            if img.width > max_w:
                max_w = img.width
            if img.height > max_h:
                max_h = img.height
            img.close()
        if max_w > cell_w or max_h > cell_h:
            uniform_scale = min(cell_w / max_w, cell_h / max_h)
            print(f'Uniform scale: {uniform_scale:.3f} (max frame {max_w}x{max_h})')

    for i, path in enumerate(paths):
        img = Image.open(path).convert('RGBA')

        # Load hotspot metadata
        hx, hy = load_hotspot(path)

        if uniform_scale < 1.0:
            new_w = max(1, int(img.width * uniform_scale))
            new_h = max(1, int(img.height * uniform_scale))
            img = img.resize((new_w, new_h), Image.LANCZOS)
            if hx is not None:
                hx = int(hx * uniform_scale)
                hy = int(hy * uniform_scale)

        if img.width > cell_w or img.height > cell_h:
            if args.scale <= 0:
                print(
                    f'Frame too large for cell: {os.path.basename(path)} '
                    f'({img.width}x{img.height}) > {cell_w}x{cell_h}. '
                    f'Use --fit, --scale, or a larger --cell.',
                    file=sys.stderr,
                )
                return 2

        col = i % cols
        row = i // cols
        cell_x = col * cell_w
        cell_y = row * cell_h

        if hx is not None and hy is not None:
            # Align hotspot to anchor point within cell
            x = cell_x + anchor_x - hx
            y = cell_y + anchor_y - hy
        else:
            # Fallback: center in cell
            x = cell_x + (cell_w - img.width) // 2
            y = cell_y + (cell_h - img.height) // 2

        # Clamp to cell bounds (crop if placement goes outside)
        # Calculate source crop region if the frame extends outside the cell
        src_x0 = max(0, cell_x - x)
        src_y0 = max(0, cell_y - y)
        dst_x = max(cell_x, x)
        dst_y = max(cell_y, y)
        src_x1 = min(img.width, cell_x + cell_w - x)
        src_y1 = min(img.height, cell_y + cell_h - y)

        if src_x1 > src_x0 and src_y1 > src_y0:
            cropped = img.crop((src_x0, src_y0, src_x1, src_y1))
            sheet.alpha_composite(cropped, (dst_x, dst_y))

    os.makedirs(os.path.dirname(args.out_png) or '.', exist_ok=True)
    sheet.save(args.out_png)
    print(f'Saved sprite sheet: {args.out_png} ({sheet_w}x{sheet_h}, {n} frames)')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
