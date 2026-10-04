#!/usr/bin/env python3
"""Extract building sprites from AoE2 HD SLPs into sprites/<name>.png.

Each PNG is the untouched SLP frame; a sidecar sprites/<name>.json records
the SLP, frame and hotspot. preprocess_sprites.py reduces the frame by exactly
3 around that hotspot, which the renderer puts on the centre of the
building's footprint, so nothing about a building's size or placement is
typed in by hand.

The SLP IDs are the standing graphics of the British civ (West European
set, civ 1) in empires2_x2_p1.dat, resolved BY ID through scripts/dat_by_id.js.
The same unit in other civs uses neighbouring SLP numbers (Goths/Teutons are
3 lower, e.g. archery range 21 vs 24), which is how the sets got mixed
before — check any new entry against the .dat rather than a list.

The Town Center is not here: it is a multi-part composite, see composite_tc.py.

Usage:
    python3 scripts/extract_buildings.py
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SLP_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics'
SPRITES_DIR = os.path.join(ROOT, 'sprites')

# (sprite name, unit ID, SLP ID, frame, graphic name)
BUILDINGS = [
    ('house',          70, 2223, 0, 'HOUS1NNG'),
    ('barracks',       12, 2683, 0, 'BRKS1NNG'),
    ('archery_range',  87,   24, 0, 'ARRG2NNW'),
    ('stable',        101, 1009, 0, 'STBL2NNW'),
    ('mining_camp',   584, 3495, 0, 'MINE1NNGW'),
    ('lumber_camp',   562, 3507, 0, 'SMIL1NNGW'),
    # Palisade (the in-game wall costs wood and is available in the Dark Age).
    # Frames are the five wall orientations; 2 is the free-standing post cluster.
    ('wall',           72, 1828, 2, 'WALL1N1G'),
    ('tower',          79, 2655, 0, 'WCTW1NNGW'),
    ('market',         84, 2278, 0, 'MRKT2NNW'),
    ('castle',         82,  305, 0, 'CSTL3NNW'),
    ('monastery',     104,  281, 0, 'CRCH3NNW'),
    ('university',    209, 3835, 0, 'UNIV3NNW'),
]


# Construction sites: one SLP per footprint size (CNST1..4_NN), three frames
# for the stages of building. Every building's constructionGraphic in the
# .dat points at the one matching its footprint.
CONSTRUCTION = [(1, 236), (2, 237), (3, 238), (4, 239)]


def main():
    jobs = list(BUILDINGS)
    for size, slp in CONSTRUCTION:
        for frame in range(3):
            jobs.append((f'construction_{size}_{frame}', -1, slp, frame, f'CNST{size}_NN'))
    for name, unit_id, slp, frame, gname in jobs:
        slp_path = os.path.join(SLP_DIR, f'{slp}.slp')
        if not os.path.exists(slp_path):
            sys.exit(f'SLP not found: {slp_path}')
        tmp = tempfile.mkdtemp(prefix=f'bldg_{slp}_')
        try:
            subprocess.run(['node', os.path.join(ROOT, 'extract-slp.js'), slp_path, tmp, str(frame)],
                           check=True, capture_output=True, cwd=ROOT)
            with open(os.path.join(tmp, f'frame_{frame}.json')) as f:
                meta = json.load(f)
            shutil.copyfile(os.path.join(tmp, f'frame_{frame}.png'),
                            os.path.join(SPRITES_DIR, f'{name}.png'))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        side = {'slp': slp, 'frame': frame, 'graphic': gname, 'unit': unit_id,
                'hotspotX': meta['hotspotX'], 'hotspotY': meta['hotspotY']}
        with open(os.path.join(SPRITES_DIR, f'{name}.json'), 'w') as f:
            json.dump(side, f)
            f.write('\n')
        print(f"  {name}: SLP {slp} frame {frame} ({gname}) {meta['width']}x{meta['height']} "
              f"hotspot ({meta['hotspotX']},{meta['hotspotY']})")
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
