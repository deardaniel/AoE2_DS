#!/usr/bin/env python3
"""Extract the game's unit and technology icons into sprites/.

Unit icons (icon_unit_<name>.png) are frames of interface SLP 50730, indexed
by the unit's iconId in the .dat (`db.unit(id).iconId` with
scripts/dat_by_id.js). Technology and age icons (icon_tech_<name>.png,
icon_age_<name>.png) are frames of SLP 50729, indexed by the research's
iconId (`dat.researches[id].iconId`).
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

TECH_SLP = os.path.join(os.path.dirname(ICON_SLP), '50729.slp')

# (sprite name, research ID, iconId) — in TechId order (source/tech.h), then the ages
TECH_ICONS = [
    ('tech_man_at_arms',   222, 85),
    ('tech_crossbow',      100, 29),
    ('tech_cavalier',      209, 78),
    ('tech_loom',           22,  6),
    ('tech_double_bit',    202, 70),
    ('tech_wheelbarrow',   213, 79),
    ('tech_gold_mining',    55, 15),
    ('tech_stone_mining',  278, 87),
    ('tech_bow_saw',       203, 71),
    ('tech_hand_cart',     249, 42),
    ('tech_horse_collar',   14,  2),
    ('tech_blast_furnace',  75, 21),
    ('tech_bodkin_arrow',  200, 35),
    ('tech_pikeman',       197, 36),
    ('tech_ballistics',     93, 25),
    ('tech_masonry',        50, 13),
    ('tech_chemistry',      47, 12),
    ('age_feudal',         101, 30),
    ('age_castle',         102, 31),
    ('age_imperial',       103, 32),
]


def extract(slp, table, kind):
    tmp = tempfile.mkdtemp(prefix='icons_')
    try:
        subprocess.run(['node', os.path.join(ROOT, 'extract-slp.js'), slp, tmp],
                       check=True, capture_output=True, cwd=ROOT)
        for name, obj_id, icon_id in table:
            src = os.path.join(tmp, f'frame_{icon_id}.png')
            if not os.path.exists(src):
                sys.exit(f'icon {icon_id} ({name}) not in {slp}')
            shutil.copyfile(src, os.path.join(SPRITES_DIR, f'icon_{name}.png'))
            print(f'  icon_{name}.png: {kind} {obj_id}, icon {icon_id}')
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def main():
    if not os.path.exists(TECH_SLP):
        sys.exit(f'SLP not found: {TECH_SLP}')
    extract(TECH_SLP, TECH_ICONS, 'research')
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
