#!/usr/bin/env python3
import argparse
import os
import shutil
import subprocess
import sys
import tempfile


def run(cmd):
    print('+', ' '.join(cmd))
    res = subprocess.run(cmd, check=False)
    if res.returncode != 0:
        raise SystemExit(res.returncode)


def main():
    ap = argparse.ArgumentParser(description='Extract SLP frames and pack into a sprite sheet.')
    ap.add_argument('slp', help='Path to .slp file')
    ap.add_argument('out_png', help='Output sprite sheet PNG')
    ap.add_argument('--palette', default='', help='Path to .pal file (optional)')
    ap.add_argument('--cell', default='32x32', help='Cell size WxH (default: 32x32)')
    ap.add_argument('--cols', type=int, default=4, help='Columns in sheet (default: 4)')
    ap.add_argument('--fit', action='store_true', help='Scale frames down to fit cell')
    ap.add_argument('--limit', type=int, default=0, help='Limit number of frames packed')
    ap.add_argument('--dirs', type=int, default=0,
                    help='Number of directions in SLP (samples 3 frames per dir)')
    args = ap.parse_args()

    slp = args.slp
    if not os.path.exists(slp):
        print(f'SLP not found: {slp}', file=sys.stderr)
        return 2

    tmp_dir = tempfile.mkdtemp(prefix='slp_frames_')
    try:
        cmd = ['node', 'extract-slp.js', slp, tmp_dir]
        if args.palette:
            cmd.append(args.palette)
        run(cmd)

        pack_cmd = [
            'python3', 'scripts/pack_frames.py', tmp_dir, args.out_png,
            '--cell', args.cell, '--cols', str(args.cols)
        ]
        if args.fit:
            pack_cmd.append('--fit')
        if args.dirs and args.dirs > 0:
            pack_cmd.extend(['--dirs', str(args.dirs)])
        elif args.limit and args.limit > 0:
            pack_cmd.extend(['--limit', str(args.limit)])
        run(pack_cmd)
    finally:
        shutil.rmtree(tmp_dir, ignore_errors=True)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
