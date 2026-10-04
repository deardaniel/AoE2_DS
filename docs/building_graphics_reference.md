# AoE2 Graphic Reference

Everything here was read from `empires2_x2_p1.dat` (AoE2 HD) **by ID** with
`scripts/dump_unit_graphics.js`. An earlier version of this file was built on
array-index lookups and had the wrong SLPs for most entries.

## Reading the .dat

`genie-dat` returns `dat.graphics` and each civ's `objects` as arrays with the
empty slots removed, so **array position is not the ID**: `dat.graphics[3345]`
is some other graphic. Use `scripts/dat_by_id.js`:

```js
const loadDat = require('./scripts/dat_by_id.js');
loadDat((dat, db) => {
    const tc = db.unit(109);                    // civ 1 (British) by default
    const g  = db.graphic(tc.standingGraphic0); // g.slpId, g.deltas[]
});
```

```bash
node scripts/dump_unit_graphics.js 109 87      # by unit ID
node scripts/dump_unit_graphics.js --name ARCHR
node scripts/dump_unit_graphics.js --civ 3 87  # same unit, Goths
```

Useful fields: `u.standingGraphic0`, `u.walkingGraphics0`, `u.dyingGraphic`,
`u.annexes[].objectId / misplaced0 / misplaced1`, `u.headObjectId`,
`g.slpId`, `g.frameCount`, `g.deltas[].graphicId / offsetX / offsetY`.

A graphic's deltas are extra layers drawn at `delta offset − SLP hotspot`
relative to the unit position. For buildings the unit position is the centre
of the footprint diamond. AoE2 tiles are 96×48 px; ours are 32×16.

## Architecture sets

The game uses civ 1, British (West European). The same building in another
set is a neighbouring SLP number, which makes them easy to mix up:

| Building | British / French | Goths / Teutons | Japanese / Chinese | Byzantine |
|---|---|---|---|---|
| Archery Range | 24 | 21 | 22 | 23 |
| Watch Tower | 2655 | 2652 | 2653 | 2654 |
| Monastery | 281 | 278 | 279 | 280 |
| Stone Wall | 2101 | 2098 | 2099 | 2100 |
| Stable | 1009 | 1006 | 1007 | 1008 |
| Market | 2278 | 2275 | 2276 | 2277 |
| Castle | 305 | 302 | 303 | 304 |

Graphic name suffixes are not a reliable guide to the set; compare the unit's
standing graphic across civs (`--civ N`) instead.

## Buildings in the game (`scripts/extract_buildings.py`)

| Building | Unit | SLP | Graphic | Other layers in the .dat |
|---|---|---|---|---|
| House | 70 | 2223 | HOUS1NNG | — |
| Barracks | 12 | 2683 | BRKS1NNG | — |
| Archery Range | 87 | 24 | ARRG2NNW | — |
| Stable | 101 | 1009 | STBL2NNW | — |
| Mining Camp | 584 | 3495 | MINE1NNGW | 3491 (not shipped), shadow 3487 |
| Lumber Camp | 562 | 3507 | SMIL1NNGW | 3503 (not shipped), shadow 3499 |
| Palisade ("Wall") | 72 | 1828, frame 2 | WALL1N1G | 4534 is a 1×1 placeholder |
| Watch Tower | 79 | 2655 | WCTW1NNGW | shadow 4351 |
| Market | 84 | 2278 | MRKT2NNW | — |
| Castle | 82 | 305 | CSTL3NNW | shadow 297 |
| Monastery | 104 | 281 | CRCH3NNW | shadow 273 |
| University | 209 | 3835 | UNIV3NNW | 1363 flag animation, shadow 1359 |

Only the main layer is used for these; the shadow layers are not composited
yet (the renderer draws a soft ellipse instead).

Footprints in the game are the real ones (unit radius × 2): house and camps
2×2, barracks / archery range / stable / monastery 3×3, market / university /
castle / Town Center 4×4, tower and palisade 1×1. Every sprite is reduced by
exactly 3 around its hotspot (`scripts/downscale.py`), so it fits its
footprint with no per-building scale. The farm is the exception: a single
terrain tile rather than the game's 3×3.

## Construction sites

Each building's `constructionGraphicId` points at one of four graphics chosen
by footprint size, each an SLP with three frames (the stages of building):

| Footprint | Graphic | SLP |
|---|---|---|
| 1×1 | CNST1_NN | 236 |
| 2×2 | CNST2_NN | 237 |
| 3×3 | CNST3_NN | 238 |
| 4×4 | CNST4_NN | 239 |

The renderer shows the stage for the current build progress and switches to
the finished building at 100%.

## Town Center (`scripts/composite_tc.py`)

Unit 109 "RTWC" is a main unit plus annex units 618, 619, 620 and head unit
621 "RTWC1X". The head unit's graphic 3345 lists every piece with its screen
offset. Dark Age, back to front:

| SLP | Graphic | Delta | Piece |
|---|---|---|---|
| 890 | RTWC1N1G | (0, 0) | foundation — not shipped with HD |
| 889 | RTWC1N0G | (0, 0) | ground shadow (SLP shadow commands only) |
| 891 | RTWC1NNG | (0, −48) | centre building |
| 3596 | RTWC1N4G | (0, 0) | left wing posts |
| 4612 | RTWC1N7G | (0, 0) | right wing posts |
| 3595 | RTWC1N3G | (0, 24) | left far post |
| 4611 | RTWC1N6G | (0, 24) | right far post |
| 3594 | RTWC1N2G | (0, 48) | left wing roof |
| 4610 | RTWC1N5G | (0, 48) | right wing roof |

SLPs 4639–4641 are Imperial Age right wings from other sets; an index-based
lookup returns them in place of 4610–4612.

The Dark Age TC is asymmetric (both lean-tos have their ridge running the same
way) and only stands on the back half of its 4×4 footprint — the front is open
ground. The composite is reduced by exactly 3 with the footprint centre on
`TC_ANCHOR`.

## Units (`scripts/build_unit_sheets.py`)

| Unit | ID | Stand | Walk | Attack | Die |
|---|---|---|---|---|---|
| Villager (m) | 83 | 1479 | 1484 | 1473 | 1476 |
| Militia | 74 | 993 | 997 | 987 | 990 |
| Archer | 4 | 8 | 12 | 2 | 5 |
| Knight | 38 | 669 | 673 | 663 | 666 |
| Spearman | 93 | 873 | 877 | 867 | 870 |
| Scout Cavalry | 448 | 2085 | 2089 | 2079 | 2082 |
| Sheep | 594 | 3629 | 3634 | — | 3626 |
| Battering Ram | 35 | 179 | 183 | 173 | 176 |
| Mangonel | 280 | 722 | 726 | 716 | 719 |
| Monk | 125 | 774 | 779 | 768 | 771 |

SLPs 702 / 708 / 713 are the Longbowman (`LNGBW_*`), not the Archer.

Siege units are two layers: a static body with one frame per direction and an
animated part drawn on top (delta graphics in the .dat). Ram walking = 181
body + 183 wheels; ram attacking = 171 body + 173 ram head; mangonel walking =
724 body + 726 wheels. The second body halves (182, 172, 725, and 177/178 for
the standing ram) are not shipped with HD. On its own, the animated SLP is
just wheels.

Villager jobs (stand / walk / work / carry): lumberjack 1542 / 1548 / 1535 /
1536 · miner 1558 / 1563 / 1560 / 1552 · builder 1493 / 1499 / 1496 / — ·
farmer 1509 / 1515 / 1512 / 1519 (hunter's meat carry) · forager carry 2592.
In graphic names `_FN` = standing, `_WN` = walking, `_AN` = attack, `_TN` =
task, `_CN` = carry, `_DN` = dying.
