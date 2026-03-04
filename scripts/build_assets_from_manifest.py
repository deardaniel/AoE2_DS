#!/usr/bin/env python3
"""Build sprite sheets from a manifest JSON.

Supports scale groups: entries with the same "group" field share a uniform
scale factor computed from the largest frame across all SLPs in the group.
This ensures consistent sprite sizes across stand/walk/attack animations.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from PIL import Image


def run(cmd):
    print('+', ' '.join(cmd))
    res = subprocess.run(cmd, check=False)
    if res.returncode != 0:
        raise SystemExit(res.returncode)


def find_max_frame_size(slp_path, palette_path):
    """Extract SLP to temp dir and find max frame dimensions."""
    tmp = tempfile.mkdtemp(prefix='slp_scan_')
    try:
        cmd = ['node', 'extract-slp.js', slp_path, tmp]
        if palette_path:
            cmd.append(palette_path)
        subprocess.run(cmd, check=True, capture_output=True)

        max_w, max_h = 0, 0
        for fname in os.listdir(tmp):
            if fname.endswith('.png'):
                img = Image.open(os.path.join(tmp, fname))
                if img.width > max_w:
                    max_w = img.width
                if img.height > max_h:
                    max_h = img.height
                img.close()
        return max_w, max_h
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


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

    # --- Pass 1: Compute group scale factors ---
    groups = {}  # group_name -> list of entries
    for entry in entries:
        grp = entry.get('group')
        if grp and not entry.get('skip'):
            groups.setdefault(grp, []).append(entry)

    group_scales = {}  # group_name -> scale factor
    if groups:
        print(f'\n=== Computing group scales ({len(groups)} groups) ===')
        for grp, grp_entries in groups.items():
            cell = grp_entries[0].get('cell', '32x32')
            cw, ch = [int(x) for x in cell.split('x')]
            max_w, max_h = 0, 0

            for entry in grp_entries:
                slp = entry.get('slp')
                palette = entry.get('palette', '')
                if not slp or not os.path.exists(slp):
                    continue
                w, h = find_max_frame_size(slp, palette)
                if w > max_w:
                    max_w = w
                if h > max_h:
                    max_h = h

            if max_w > cw or max_h > ch:
                scale = min(cw / max_w, ch / max_h)
            else:
                scale = 1.0
            group_scales[grp] = scale
            print(f'  Group "{grp}": max frame {max_w}x{max_h} -> scale {scale:.3f}')

    # --- Pass 2: Build all sprite sheets ---
    print(f'\n=== Building sprite sheets ===')
    for entry in entries:
        if entry.get('skip'):
            print(f"Skipping {entry.get('name', '?')} (manual composite)")
            continue
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

        # Use group scale if available, otherwise fall back to --fit
        grp = entry.get('group')
        if grp and grp in group_scales:
            cmd.extend(['--scale', str(group_scales[grp])])
        elif entry.get('fit'):
            cmd.append('--fit')

        dirs = entry.get('dirs')
        if dirs:
            cmd.extend(['--dirs', str(dirs)])
            fpd = entry.get('fpd')
            if fpd:
                cmd.extend(['--fpd', str(fpd)])
        else:
            limit = entry.get('limit')
            if limit:
                cmd.extend(['--limit', str(limit)])

        run(cmd)

    return 0


if __name__ == '__main__':
    raise SystemExit(main())
