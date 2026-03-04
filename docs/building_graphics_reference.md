# AoE2 Building Graphic Reference

## How to parse the .dat file

The `genie-dat` npm package (already installed) can parse AoE2 HD .dat files:

```js
const genieDat = require('genie-dat');
const datBuf = fs.readFileSync('empires2_x2_p1.dat');
genieDat.load(datBuf, {version: 'african-kingdoms'}, function(err, dat) {
    // dat.civilizations[civIdx].objects[unitId]  — unit data
    // dat.graphics[graphicId]                     — graphic data
    // Key fields:
    //   unit.standingGraphic0  → graphic ID
    //   graphic.slpId          → SLP file number
    //   graphic.deltas[]       → sub-graphic layers
    //     delta.graphicId      → sub-graphic ID
    //     delta.offsetX/Y      → pixel offset for compositing
});
```

**IMPORTANT**: The `{version: 'african-kingdoms'}` flag is required for AoE2 HD.

DAT file location: `/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/empires2_x2_p1.dat`

## Architecture Set Suffixes

SLP/Graphic names use a suffix letter for architecture variant:
- **E** = East European (Slavs, Magyars, etc.)
- **F** = Far East / Asian (Chinese, Japanese, etc.)
- **M** = Middle Eastern (Saracens, Turks, etc.)
- **W** = West European (Britons, Franks, etc.)
- **G** = Generic (shared across all architectures, especially Dark Age)
- **I** = Indian (added in expansions)
- **X** = Preview/combined (used for building placement preview)

Civ 1 = Britons = West European architecture set.

## Town Center (RTWC) — Unit 99

The Dark Age TC is the most complex building. It's a multi-layer composite:

### RTWC1X Preview Graphic (Graphic 3345, SLP=-1)

This is the full composite with 9 delta layers:

| Delta | Graphic ID | SLP  | Name       | Offset (X,Y) | Layer | Role                     |
|-------|-----------|------|------------|---------------|-------|--------------------------|
| 0     | 434       | 890  | RTWC1N1G   | (0, 0)        | 10    | Foundation (FILE MISSING) |
| 1     | 433       | 889  | RTWC1N0G   | (0, 0)        | 5     | Selection/damage outline (RED — skip) |
| 2     | 435       | 891  | RTWC1NNG   | (0, -48)      | 20    | Center building          |
| 3     | 3241      | 3596 | RTWC1N4G   | (0, 0)        | 20    | Wing pillars/posts       |
| 4     | 5470      | 4641 | RTWC4N6E   | (0, 0)        | 20    | Wing columns             |
| 5     | 3240      | 3595 | RTWC1N3G   | (0, 24)       | 20    | Right wing single pillar |
| 6     | 5469      | 4640 | RTWC4N5W   | (0, 24)       | 20    | Right wing canopy        |
| 7     | 3239      | 3594 | RTWC1N2G   | (0, 48)       | 20    | Left wing roof           |
| 8     | 5468      | 4639 | RTWC4N5M   | (0, 48)       | 20    | Left wing canopy         |

Note: Deltas 4/6/8 are architecture-specific (E/W/M mixed in the preview).

### N5 Canopy Variants (wing pavilion roofs)

| SLP  | Suffix | Style                | Visual                           |
|------|--------|----------------------|----------------------------------|
| 4610 | G      | Generic Dark Age     | Light thatch canopy, wood pillar |
| 4637 | E      | East European        | Terracotta tile roof, stone      |
| 4638 | F      | Far East / Asian     | Bamboo/reed curved roof          |
| 4639 | M      | Middle Eastern       | Flat sandstone pavilion, ornate  |
| 4640 | W      | West European (Imp.) | Dark slate roof, small trees     |

### N6 Column Variants (wing support columns)

| SLP  | Suffix | Style                |
|------|--------|----------------------|
| 4611 | G      | Generic Dark Age     |
| 4641 | E      | East European        |
| 4642 | F      | Far East / Asian     |
| 4643 | M      | Middle Eastern       |
| 4644 | W      | West European (Imp.) |

### Current composite_tc.py configuration

Uses SLP 4639 (M) for both wing canopies (user confirmed M looks correct)
and SLP 4641 (E) for columns (small, barely visible at NDS resolution).

### Annex Sub-Units

| Unit ID | Name   | StandingGraphic SLP | Role            |
|---------|--------|---------------------|-----------------|
| 487     | RTWC1A | 891 (RTWC1NNG)      | Center building |
| 488     | RTWC1B | 3595 (RTWC1N3G)     | Right wing      |
| 489     | RTWC1C | 3594 (RTWC1N2G)     | Left wing       |
| 490     | RTWC1X | -1 (preview only)   | Combined view   |

## Other Buildings — Correct SLP Mapping (Civ 1 / West European)

From .dat file, civ 1 (Britons):

| Building       | Unit ID | Graphic ID | SLP  | Graphic Name | Notes                    |
|----------------|---------|-----------|------|--------------|--------------------------|
| House          | 63      | 2216      | 2223 | HOUS1NNG     | Dark Age generic         |
| Barracks       | 12      | 2664      | 2683 | BRKS1NNG     | Dark Age generic         |
| Archery Range  | 80      | 17        | 24   | ARRG2NNW     | Feudal Age West European |
| Stable         | 92      | 1003      | 1009 | STBL2NNW     | Feudal Age West European |
| Mining Camp    | 453     | 3118      | 3495 | MINE1NNGW    | Dark Age West European   |
| Lumber Camp    | 431     | 3122      | 3507 | SMIL1NNGW    | Dark Age West European   |
| Mill           | 62      | 3114      | 3482 | MILL1N1G     | Dark Age generic         |
| Farm           | 48      | 181       | 419  | FARM0NNG     | SLP file doesn't exist   |
| Town Center    | 99      | 3241      | 3596 | RTWC1N4G     | Composite — see above    |

### Previously WRONG SLPs (East European variants used in old manifest)

| Building       | Wrong SLP | Wrong Name  | Correct SLP | Correct Name |
|----------------|-----------|-------------|-------------|--------------|
| House          | 2232      | HOUS2NNE    | 2223        | HOUS1NNG     |
| Barracks       | 130       | BRKS2NNE    | 2683        | BRKS1NNG     |
| Archery Range  | 21        | ARRG2NNE    | 24          | ARRG2NNW     |
| Stable         | 1006      | STBL2NNE    | 1009        | STBL2NNW     |
| Mining Camp    | 3492      | MINE1NNGE   | 3495        | MINE1NNGW    |
| Lumber Camp    | 3504      | SMIL1NNGE   | 3507        | SMIL1NNGW    |

## Graphic Name Convention

Format: `TYPE + AGE + N + PART + SUFFIX`

- **TYPE**: RTWC (TC), HOUS (House), BRKS (Barracks), ARRG (Arch. Range),
  STBL (Stable), MINE (Mining Camp), SMIL (Lumber Camp = "Small Mill"),
  MILL (Mill), FARM (Farm)
- **AGE**: 0=all ages, 1=Dark, 2=Feudal, 3=Castle, 4=Imperial
- **N**: separator
- **PART**: NN=standing, N0=shadow, N1=foundation, N2-N6=detail layers
- **SUFFIX**: G/E/F/M/W/I/X (architecture variant)

## Player Colors

AoE2 player 1 (blue) colors from 50500.bina palette indices 16-23:

| Index | R   | G   | B   | Description          |
|-------|-----|-----|-----|----------------------|
| 16    | 0   | 0   | 82  | Very dark blue       |
| 17    | 0   | 21  | 130 | Dark blue            |
| 18    | 19  | 49  | 161 | Medium blue          |
| 19    | 48  | 93  | 182 | Medium-light blue    |
| 20    | 74  | 121 | 208 | Light blue           |
| 21    | 110 | 166 | 235 | Sky blue             |
| 22    | 151 | 206 | 255 | Very light blue      |
| 23    | 205 | 250 | 255 | Near white/cyan      |

These are reserved at NDS palette indices 16-23 by `preprocess_sprites.py`.
Red variants (for player 2) are at NDS palette indices 246-253.

## Tools

- `scripts/dump_tc_graphics.js` — Dumps TC graphic chain from .dat file
- `scripts/composite_tc.py` — Composites TC from 7 SLP layers with delta Y offsets
- `scripts/build_sprite_sheet.py` — General SLP→sprite sheet pipeline
- `scripts/build_assets_from_manifest.py` — Builds all sprites from manifest_game.json
- `extract-slp.js` — Extracts individual SLP frames to PNG with hotspot metadata
