#!/usr/bin/env python3
import argparse
import json
import os
import subprocess
import sys


def run(cmd):
    print('+', ' '.join(cmd))
    res = subprocess.run(cmd, check=False)
    if res.returncode != 0:
        raise SystemExit(res.returncode)


def main():
    ap = argparse.ArgumentParser(description='Build sprite sheets from a manifest JSON.')
    ap.add_argument('manifest', help='Path to manifest JSON')
    args = ap.parse_args()

    if not os.path.exists(args.manifest):
        print(f'Manifest not found: {args.manifest}', file=sys.stderr)
        return 2

    with open(args.manifest, 'r', encoding='utf-8') as f:
        data = json.load(f)

    entries = data.get('entries', [])
    if not entries:
        print('Manifest has no entries.', file=sys.stderr)
        return 2

    for entry in entries:
        slp = entry.get('slp')
        out_png = entry.get('out')
        if not slp or not out_png:
            print('Entry missing required fields (slp, out):', entry, file=sys.stderr)
            return 2

        cmd = ['python3', 'scripts/build_sprite_sheet.py', slp, out_png]
        palette = entry.get('palette')
        if palette:
            cmd.extend(['--palette', palette])
        cell = entry.get('cell')
        if cell:
            cmd.extend(['--cell', cell])
        cols = entry.get('cols')
        if cols:
            cmd.extend(['--cols', str(cols)])
        if entry.get('fit'):
            cmd.append('--fit')
        limit = entry.get('limit')
        if limit:
            cmd.extend(['--limit', str(limit)])

        run(cmd)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
