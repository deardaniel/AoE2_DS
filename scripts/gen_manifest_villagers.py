#!/usr/bin/env python3
import argparse
import json
import os
import re
import struct

SLP_HDR_OFF = 4
FRAME_HDR_OFF = 32
FRAME_HDR_SIZE = 32


def read_slp_info(path):
    with open(path, 'rb') as f:
        f.seek(0, os.SEEK_END)
        size = f.tell()
        f.seek(0)
        buf = f.read(FRAME_HDR_OFF)
        if len(buf) < FRAME_HDR_OFF:
            return None
        num_frames = struct.unpack_from('<I', buf, SLP_HDR_OFF)[0]
        frames = []
        f.seek(FRAME_HDR_OFF)
        for _ in range(num_frames):
            data = f.read(FRAME_HDR_SIZE)
            if len(data) < FRAME_HDR_SIZE:
                break
            width, height = struct.unpack_from('<ii', data, 16)
            frames.append((width, height))
        if not frames:
            return None
        max_w = max(w for w, _ in frames)
        max_h = max(h for _, h in frames)
        return {
            'num_frames': num_frames,
            'max_w': max_w,
            'max_h': max_h,
            'size': size,
        }


def main():
    ap = argparse.ArgumentParser(description='Generate a manifest of likely villager SLPs (heuristic).')
    ap.add_argument('--graphics-dir', default='/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics', help='Age2HD graphics directory')
    ap.add_argument('--palette', default='/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/pal_5.pal', help='Palette path')
    ap.add_argument('--out', default='assets/manifest_villagers.json', help='Output manifest path')
    ap.add_argument('--min-frames', type=int, default=24, help='Minimum frame count')
    ap.add_argument('--max-frames', type=int, default=90, help='Maximum frame count')
    ap.add_argument('--min-size', type=int, default=16, help='Minimum max dimension')
    ap.add_argument('--max-size', type=int, default=48, help='Maximum max dimension')
    ap.add_argument('--limit', type=int, default=50, help='Max entries to include')
    args = ap.parse_args()

    files = [f for f in os.listdir(args.graphics_dir) if f.lower().endswith('.slp')]
    files.sort(key=lambda s: int(re.sub(r'\D', '', s) or 0))

    candidates = []
    for name in files:
        path = os.path.join(args.graphics_dir, name)
        info = read_slp_info(path)
        if not info:
            continue
        nf = info['num_frames']
        max_w = info['max_w']
        max_h = info['max_h']
        max_dim = max(max_w, max_h)
        if nf < args.min_frames or nf > args.max_frames:
            continue
        if max_dim < args.min_size or max_dim > args.max_size:
            continue
        area = max_w * max_h
        candidates.append((area, nf, name, info))

    candidates.sort(key=lambda x: (x[1], x[0]))
    entries = []
    for _, _, name, info in candidates[: args.limit]:
        slp_path = os.path.join(args.graphics_dir, name)
        max_w = info['max_w']
        max_h = info['max_h']
        cell = '32x32' if max_w <= 32 and max_h <= 32 else '64x64'
        out_png = f"sprites/vil_{os.path.splitext(name)[0]}.png"
        entries.append({
            'name': f"vil_{os.path.splitext(name)[0]}",
            'slp': slp_path,
            'palette': args.palette,
            'out': out_png,
            'cell': cell,
            'cols': 4,
            'fit': True,
        })

    os.makedirs(os.path.dirname(args.out) or '.', exist_ok=True)
    with open(args.out, 'w', encoding='utf-8') as f:
        json.dump({'entries': entries}, f, indent=2)

    print(f'Wrote {len(entries)} entries to {args.out}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
