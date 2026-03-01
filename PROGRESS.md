# AoE2 DSi — Implementation Progress

## Build Status: COMPILES SUCCESSFULLY (all fixes applied)

## Phase Status

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| 1 | Config & Architecture Refactor | DONE | config.h, game.h/cpp, input.h/cpp, render.h/cpp |
| 2 | Enhanced Terrain & Map Generation | DONE | All 7 terrain types, procedural gen, resources |
| 3 | Unit System | DONE | 5 unit types, A* pathfinding, movement |
| 4 | Building System | DONE | 8 building types, training queues, TC arrows |
| 5 | Economy | DONE | Gathering, drop-offs, farms |
| 6 | Combat | DONE | Unit-vs-unit + unit-vs-building combat |
| 7 | Top Screen — Minimap + Info Panel | DONE | Minimap, console, resources |
| 8 | Fog of War | DONE | 3-state fog, checkerboard explored effect |
| 9 | Technology / Ages | DONE | Tech stats used in combat/spawning/LOS/speed |
| 10 | AI Opponent | DONE | Attack-move, retreat logic, building attacks |
| 11 | Sound | PARTIAL | 8 WAV SFX extracted to audio/, not yet wired to maxmod |
| 12 | Game Flow & Polish | DONE | Win/lose, restart, age-up, tech research UI |
| 13 | HD Sprite Extraction | DONE | Palette fixed (50500.bina), 5 unit types + 30 HD sprites extracted |
| 14 | HD Sprite Integration | DONE | All procedural sprites replaced with HD art, 32×32 units, player colors |

## Bug Fixes Applied

### Critical Fixes
1. **Units can attack buildings** — Added `attackBldgTarget` field, building attack logic in unit_update_attacking(), input handling for tapping enemy buildings
2. **Buildings die from damage** — Added HP <= 0 check in buildings_update() calling building_destroy()
3. **Tech bonuses applied everywhere** — Combat, spawning, LOS, speed all use playerUnitStats instead of base UNIT_STATS
4. **AI attacks properly** — AI commands attack on enemy units/buildings, retreat when army < 2
5. **Chase attack fixed** — Save/restore attackTarget/attackBldgTarget around unit_command_move() calls (was being cleared)
6. **START button works** — Removed duplicate scanKeys() that was consuming input state
7. **OAM VRAM leak on restart fixed** — Free all OAM gfx before reinitializing game
8. **Sprite tile format fixed** — Added linear_to_tiled() conversion for OAM sprite data (8x8 tile layout)
9. **Gather rate fixed** — Added separate gatherTick field to avoid double-increment with animTick
10. **Stale selection cleared** — selectedUnit/selectedBldg cleared when entity dies

### Player Controls
- **START** on selected TC: Age advancement (priority) or train villager
- **START** on selected military building: Research tech (if available) or train unit
- **Touch enemy building** with unit selected: Attack building
- **Touch enemy unit** with unit selected: Attack unit
- **X**: Toggle build menu
- **B**: Cancel action
- **L/R**: Cycle idle villagers
- **Y**: Center on TC
- **SELECT**: Toggle follow cam
- **D-pad**: Camera scroll

### Asset Pipeline Fix
- **Root cause**: Previous agent used `pal_5.pal` (terrain palette with muddy greens) instead of `50500.bina` (the actual AoE2 unit rendering palette with proper player colors)
- **Fix**: Updated `extract-slp.js` palette search order to prioritize `50500.bina` from `drs/interface/`
- **Fix**: Updated `gen_manifest_hd.py` default palette to `50500.bina`
- **Fix**: Updated all manifest files (`manifest.json`, `manifest_hd.json`) to use correct palette
- **Fix**: Raised frame size limit from 256px to 400px in extract-slp.js (buildings were being filtered)
- **Result**: All sprites now have correct AoE2 colors (blue player color, natural skin tones, proper armor/weapon colors)
- **SLP IDs**: Verified against [openage aoc-slp-list](https://github.com/SFTtech/openage/blob/master/doc/media/aoc-slp-list.md)

### Correct SLP ID Mapping (openage-verified)
| Entity | Stand SLP | Fight/Fire SLP | Walk SLP |
|--------|-----------|----------------|----------|
| Villager (M) | 1479 | 1473 | 1484 |
| Villager (F) | 1388 | 1382 | 1392 |
| Militia | 993 | 987 | 997 |
| Archer | 708 | 702 | 713 |
| Knight | 669 | 663 | 673 |
| Spearman | 873 | 867 | 877 |

| Building | SLP | Notes |
|----------|-----|-------|
| Town Center | 900 | North European, Feudal |
| House | 2232 | North European, Feudal |
| Barracks | 130 | North European, Feudal |
| Archery Range | 21 | North European, Feudal |
| Stable | 1006 | North European, Feudal |
| Mining Camp | 3492 | North European |
| Lumber Camp | 3504 | North European |

### Extracted Game Sprites (18 total)
- **Units (11)**: villager, villager_walk, villager_f, militia, militia_fight, archer, archer_fire, knight, knight_fight, spearman, spearman_fight
- **Buildings (7)**: town_center, house, barracks, archery_range, stable, mining_camp, lumber_camp
- **Makefile target**: `make assets-game`

### Unit Stats Update (wiki-verified)
Updated config.h to match real AoE2 values:
- Militia: 40 HP, 4 ATK, 1 ARM (was 10 ATK — way too high)
- Archer: 30 HP, 4 ATK, range 4, costs 25W/45G (no food)
- Knight: 100 HP, 10 ATK, 2 ARM (was 80 HP/12 ATK/3 ARM)
- Spearman: +15 bonus vs cavalry (was ×2 damage)
- Train times scaled to real seconds × 60fps
- Building HP scaled up (TC 600, Barracks/etc 350)

### Sound Effects
Extracted 8 candidate WAV files from AoE2 HD (`drs/sounds/`) to `audio/`:
- sfx_click.wav (0.05s), sfx_arrow.wav (0.07s), sfx_sword.wav (0.23s)
- sfx_build_place.wav (0.37s), sfx_chop.wav (0.13s)
- sfx_select.wav (0.57s), sfx_death.wav (0.54s), sfx_complete.wav (0.47s)
- Total: 128KB — ready for maxmod integration

### Cleanup
- Removed: debug scripts (probe-slp.js, extract-slp-big.js), debug images (debug_raw.pgm), old sprites (villager.bmp/.ppm/_old.png, index.html), wrong-palette vil_*.png (20 files), stale scripts (gen_manifest_villagers.py, gen_sprite_index.py), stale manifest (manifest_villagers.json)

### HD Sprite Integration (Phase 14)
- **Preprocessing pipeline**: `scripts/preprocess_sprites.py` converts all HD sprite PNGs to indexed binary data with shared 256-color palette
- **Shared palette**: All 18 sprites (11 unit sheets + 7 buildings) quantized to 177 unique colors, stored in `data/sprite_pal.bin` (512 bytes, BGR555 format)
- **Player color remap**: Blue→Red remap table (`data/sprite_remap.bin`) for player 2 units/buildings. 7 blue shades auto-detected and mapped to red variants.
- **Unit sprites upgraded to 32×32 OAM** (from 16×16) for HD detail. Centered on tile with -8px offset.
- **Direction mapping**: AoE2 SLP 5-frame directions (S,SW,W,NW,N) mapped to 4-direction system. East = hFlip of West (done in software before tiling).
- **Animation**: Walk/fight sheets use 3 animation frames per direction (15 frames total, 5 dirs × 3 anims). Stand sheets use 1 frame per direction.
- **State→sheet mapping**: Idle→stand, Moving/Returning→walk, Attacking/Gathering→fight. Falls back to stand if no specific sheet exists.
- **Death effect**: All visible pixels mapped to palette index 255 (dark gray)
- **Buildings**: 64×64 source PNGs scaled to 32×32 (2×2 tile) or 16×16 (1×1 tile). Under-construction buildings show outline pattern.
- **Total binary sprite data**: 170.5 KB in `data/` directory
- **ROM size**: 546 KB (up from ~350 KB with procedural sprites)
- **Build**: `make sprites` to regenerate binary data from PNGs, then `make` to build ROM

### Known Limitations (by design)
- Sound is stubbed (no maxmod integration yet)
- Buildings auto-construct (no builder villager required)
- No Mill building — only TC accepts food
- Path length capped at 64 steps (re-path needed for very long paths)
- Tech HP bonus only applies to newly spawned units, not existing ones
- Terrain tiles (forest, gold, stone, farm) still use procedural palette-based graphics

## File Manifest
```
source/main.cpp      - Entry point, main loop, game start
source/config.h      - Constants, enums, stat tables, palette indices
source/game.h/cpp    - GameState, Player, resources, win/lose
source/terrain.h/cpp - TerrainMap, procedural generation, tile cache
source/units.h/cpp   - Unit pool, A* pathfinding, combat, gathering
source/buildings.h/cpp - Building pool, training, TC arrows, destruction
source/input.h/cpp   - Touch + button input dispatch
source/render.h/cpp  - OAM sprites, HD sprite loading, tile conversion
source/ui.h/cpp      - Top screen minimap + console info panel
source/fog.h/cpp     - Fog of war (2-player, 3-state)
source/tech.h/cpp    - Age advancement + 3 unit upgrades
source/ai.h/cpp      - AI opponent (economy, military, building, attack)
source/sound.h/cpp   - Sound stubs
data/sprite_pal.bin  - Shared 256-color NDS palette for all OAM sprites
data/sprite_remap.bin - Player 2 color remap table (blue→red)
data/spr_*.bin       - Indexed sprite data for units and buildings
scripts/preprocess_sprites.py - PNG→binary sprite preprocessor
```
