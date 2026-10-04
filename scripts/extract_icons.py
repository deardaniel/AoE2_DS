#!/usr/bin/env python3
"""Extract the game's unit icons into sprites/icon_unit_<name>.png.

The icons are frames of interface SLP 50730, indexed by the unit's iconId in
the .dat (looked up by ID: `db.unit(id).iconId` with scripts/dat_by_id.js).
They are 36x36; preprocess_sprites.py scales them to the 32x32 menu slots.

Usage:
    python3 scripts/extract_icons.py
"""
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICON_SLP = ('/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/'
            '_common/drs/interface/50730.slp')
SPRITES_DIR = os.path.join(ROOT, 'sprites')

# (sprite name, unit ID, iconId)
UNIT_ICONS = [
    ('villager',  83, 15),
    ('militia',   74,  8),
    ('archer',     4, 17),
    ('knight',    38,  1),
    ('spearman',  93, 31),
    ('scout',    448, 64),
    ('ram',       35, 74),
    ('mangonel', 280, 27),
    ('monk',     125, 33),
]


def main():
    if not os.path.exists(ICON_SLP):
        sys.exit(f'SLP not found: {ICON_SLP}')
    tmp = tempfile.mkdtemp(prefix='icons_')
    try:
        subprocess.run(['node', os.path.join(ROOT, 'extract-slp.js'), ICON_SLP, tmp],
                       check=True, capture_output=True, cwd=ROOT)
        for name, unit_id, icon_id in UNIT_ICONS:
            src = os.path.join(tmp, f'frame_{icon_id}.png')
            if not os.path.exists(src):
                sys.exit(f'icon {icon_id} ({name}) not in {ICON_SLP}')
            shutil.copyfile(src, os.path.join(SPRITES_DIR, f'icon_unit_{name}.png'))
            print(f'  icon_unit_{name}.png: unit {unit_id}, icon {icon_id}')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
