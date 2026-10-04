# AoE2 DSi — Implementation Progress

## Build Status: COMPILES SUCCESSFULLY (all fixes applied)

## Phase Status

| Phase | Description | Status | Notes |
|-------|-------------|--------|-------|
| 1 | Config & Architecture Refactor | DONE | config.h, game.h/cpp, input.h/cpp, render.h/cpp |
| 2 | Enhanced Terrain & Map Generation | DONE | All 7 terrain types, procedural gen, resources |
| 3 | Unit System | DONE | 6 unit types, A* pathfinding, movement |
| 4 | Building System | DONE | 8 building types, training queues, TC arrows |
| 5 | Economy | DONE | Gathering, drop-offs, farms |
| 6 | Combat | DONE | Unit-vs-unit + unit-vs-building combat |
| 7 | Top Screen — Minimap + Info Panel | DONE | Minimap, console, resources |
| 8 | Fog of War | DONE | 3-state fog, checkerboard explored effect |
| 9 | Technology / Ages | DONE | Tech stats used in combat/spawning/LOS/speed |
| 10 | AI Opponent | DONE | Attack-move, retreat logic, building attacks |
| 11 | Sound | DONE | NitroFS music streaming + 14 SFX via maxmod |
| 12 | Game Flow & Polish | DONE | Win/lose, restart, age-up, tech research UI |
| 13 | HD Sprite Extraction | DONE | Palette fixed (50500.bina), 6 unit types + buildings extracted |
| 14 | HD Sprite Integration | DONE | All procedural sprites replaced with HD art, 32×32 units, player colors |
| 15 | Unit Collision Avoidance | DONE | Tile occupancy grid, formation spreading, nudge/wait/repath |
| 16 | Sheep as Units | DONE | Sheep spawn near TCs, gatherable food source, walk/die animations |
| 17 | Berry/Work Animation Fixes | DONE | Real berry sprite, correct work SLPs, forager carry animation |
| 18 | Idle Animations | DONE | Units cycle through standing frames when idle |
| 19 | TC Composite Sprite | DONE | Rebuilt 2026-10-04 from the 9 Dark Age layers at exact 1/3 scale |
| 20 | Sprite audit | DONE | All SLPs re-resolved by ID; buildings, units and layouts rebuilt |

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
11. **Unit collision avoidance** — Tile occupancy grid, movement collision (nudge/wait/repath), formation spreading, pathfinding routes around stationary units
12. **Sheep as gatherable units** — Sheep spawn near TCs (4 per player), villagers attack to gather food, excluded from pop cap and enemy targeting
13. **Berry sprite replaced** — Procedural spr_berries.bin replaced with real SLP 2560 game asset
14. **Work animation SLPs corrected** — farmer_work was SLP 1506 (VMFAR_DN = dying!), fixed to SLP 1473 (VMBAS_AN). builder_work was SLP 1490 (VMBLD_DN = dying!), fixed to SLP 1496 (VMBLD_TN)
15. **Forager carry animation** — Added VROLE_FORAGER role; berry gatherers carry basket (SLP 2592 VMFOR_CN) distinct from meat carriers
16. **Idle animations** — Units cycle through 5 standing frames when idle at reduced speed
17. **TC composite sprite** — Dark Age layers 889, 891, 3594-3596, 4610-4612 placed by hotspot + delta (the earlier 4639-4641 were Imperial Age pieces)
18. **Sprite audit (2026-10-04)** — .dat lookups were by array index instead of ID. Fixed: Archer used Longbowman graphics; tower, monastery, camps, archery range, university and wall came from other civ sets or ages; standing sheets held one direction while the renderer read five; units were each scaled differently

### Player Controls
- **START** on selected TC: Age advancement (priority) or train villager
- **START** on selected military building: Research tech (if available) or train unit
- **Touch enemy building** with unit selected: Attack building
- **Touch enemy unit** with unit selected: Attack unit
- **A**: Ungarrison TC (if selected with garrison), else select first military unit
- **X**: Toggle build menu
- **B**: Cancel action
- **L/R**: Cycle idle villagers
- **Y**: Center on TC
- **SELECT**: Toggle follow cam
- **D-pad**: Camera scroll

### Asset Pipeline Fix
- **Root cause**: Previous agent used `pal_5.pal` (terrain palette with muddy greens) instead of `50500.bina` (the actual AoE2 unit rendering palette with proper player colors)
- **Fix**: Updated `extract-slp.js` palette search order to prioritize `50500.bina` from `drs/interface/`
- **Fix**: Raised frame size limit from 256px to 400px in extract-slp.js (buildings were being filtered)
- **Result**: All sprites now have correct AoE2 colors (blue player color, natural skin tones, proper armor/weapon colors)
- **SLP IDs**: Verified against [openage aoc-slp-list](https://github.com/SFTtech/openage/blob/master/doc/media/aoc-slp-list.md)

### SLP IDs
See `docs/building_graphics_reference.md` — every ID there was read from the
.dat by ID. The tables that used to be here were wrong (Archer was the
Longbowman, several buildings were from other civ sets).

### Unit Stats (wiki-verified)
Updated config.h to match real AoE2 values:
- Militia: 40 HP, 4 ATK, 1 ARM (was 10 ATK — way too high)
- Archer: 30 HP, 4 ATK, range 4, costs 25W/45G (no food)
- Knight: 100 HP, 10 ATK, 2 ARM (was 80 HP/12 ATK/3 ARM)
- Spearman: +15 bonus vs cavalry (was ×2 damage)
- Train times scaled to real seconds × 60fps
- Building HP scaled up (TC 600, Barracks/etc 350)

### Villager Role Sprites
| Role | Stand/Walk | Work SLP | Carry SLP | Notes |
|------|-----------|----------|-----------|-------|
| Base (VROLE_BASE) | villager_stand/walk | — | — | Default |
| Lumberjack (VROLE_LUMBERJACK) | lumberjack_stand/walk | 1560 (VMMIN_TN) | lumberjack_carry | Wood gathering |
| Miner (VROLE_MINER) | miner_stand/walk | 1560 (VMMIN_TN) | miner_carry | Gold/stone mining |
| Builder (VROLE_BUILDER) | builder_stand/walk | 1496 (VMBLD_TN) | — | Construction |
| Farmer (VROLE_FARMER) | farmer_stand/walk | 1473 (VMBAS_AN) | farmer_carry | Farm/meat gathering |
| Forager (VROLE_FORAGER) | farmer_stand/walk | 1473 (VMBAS_AN) | 2592 (VMFOR_CN) | Berry gathering (basket) |

### Sound Effects
14 SFX via maxmod + NitroFS background music streaming. Sound fully integrated.

### HD Sprite Integration (Phase 14)
- **Preprocessing pipeline**: `scripts/preprocess_sprites.py` converts all HD sprite PNGs to indexed binary data with shared 256-color palette
- **Shared palette**: All sprites quantized to shared colors, stored in `data/sprite_pal.bin` (512 bytes, BGR555 format)
- **Player color remap**: Blue→Red remap table (`data/sprite_remap.bin`) for player 2 units/buildings. 7 blue shades auto-detected and mapped to red variants.
- **Unit sprites upgraded to 32×32 OAM** (from 16×16) for HD detail. Centered on tile with -8px offset.
- **Direction mapping**: AoE2 SLP 5-frame directions (S,SW,W,NW,N) mapped to 8-direction system with mirroring.
- **Animation**: Walk/fight sheets use 10 animation frames per direction. Stand sheets use 5 frames per direction.
- **State→sheet mapping**: Idle→stand, Moving/Returning→walk, Attacking/Gathering→fight. Falls back to stand if no specific sheet exists.
- **Death effect**: All visible pixels mapped to palette index 255 (dark gray)
- **Buildings**: HD source PNGs scaled to fit 32×32 (1×1 tile), 64×64 (2×2 tile), or 128×128 (4×4 tile). Under-construction buildings show outline pattern.
- **Build**: `make sprites` to regenerate binary data from PNGs, then `make` to build ROM

### Unit Collision Avoidance (Phase 15)
- **Tile occupancy grid**: `tileOccupant[32][32]` rebuilt every frame, tracks which unit occupies each tile
- **Formation spreading**: Move commands assign spiral pattern destinations (center → ring-1 → ring-2) so units spread out
- **Movement collision**: Before stepping to next tile, check occupancy — nudge idle friendlies, wait for moving friendlies, repath after 8 frames, allow enemy overlap for combat
- **Pathfinding awareness**: A* marks stationary units (idle/gathering/building/attacking) as impassable, skips moving/scouting units
- **Spawn/ungarrison**: Only place units on unoccupied passable tiles
- **Gather/build preference**: Prefer unoccupied adjacent tiles when multiple villagers work the same resource or building

### Known Limitations (by design)
- No Mill building — only TC accepts food
- Path length capped at 64 steps (re-path needed for very long paths)
- Tech HP bonus only applies to newly spawned units, not existing ones

## File Manifest

### Source Code
```
source/main.cpp       - Entry point, main loop, phase management, rendering pipeline
source/config.h       - Constants, enums, stat tables, palette indices
source/game.h/cpp     - GameState, Player, resources, selection, win/lose
source/terrain.h/cpp  - TerrainMap, procedural generation, isometric tile rendering
source/units.h/cpp    - Unit pool, A* pathfinding, combat, gathering, collision avoidance
source/buildings.h/cpp - Building pool, training, TC arrows, garrison, destruction
source/input.h/cpp    - Touch + button input, drag-select, build menu, formation spreading
source/render.h/cpp   - Software sprite rendering, HD sprite sheets, depth sorting
source/ui.h/cpp       - Top screen minimap + console HUD, bitmap font rendering
source/fog.h/cpp      - Fog of war (2-player, 3-state)
source/tech.h/cpp     - Age advancement + 3 unit upgrades
source/ai.h/cpp       - AI opponent (economy, military, building, attack)
source/sound.h/cpp    - NitroFS music streaming + 14 SFX via maxmod
source/font.h/cpp     - Bitmap font (Century Bold 14px) for both screens
source/iso.h          - Isometric coordinate conversions, diamond mask tables
source/res_icons.h    - Resource icon pixel data
```

### Asset Pipeline
```
extract-slp.js                    - SLP→PNG frame extractor (Node.js, uses genie-slp)
scripts/dat_by_id.js              - .dat loader that indexes graphics and units by ID
scripts/dump_unit_graphics.js     - Print a unit's graphics/SLPs, deltas and annexes
scripts/composite_tc.py           - Town Center from its nine SLP layers
scripts/extract_buildings.py      - Building frames + hotspot sidecars
scripts/build_unit_sheets.py      - Every unit sprite sheet (one layout, one scale)
scripts/preprocess_sprites.py     - PNG→NDS indexed binary converter (shared palette)
scripts/preprocess_terrain.py     - HD terrain textures→NDS binary tiles
scripts/shared_constants.py       - Constants shared with the C++ (palette, iso geometry, anchors)
scripts/build_sprite_sheet.py     - One-off single-SLP sheet (calls pack_frames.py)
```

### Binary Data (generated by `make sprites`)
```
data/sprite_pal.bin    - Shared 256-color NDS palette (BGR555)
data/sprite_remap.bin  - Player 2 color remap table (blue→red)
data/spr_*.bin         - Indexed sprite data for all units and buildings
data/terrain_tiles.bin - Isometric terrain tiles indexed against sprite palette
data/terrain_pal.bin   - UI palette overlay for console text colors
data/font_aoe2.bin     - Bitmap font glyph data
```

### Build Targets
```
make                - Compile C++ and link .nds ROM
make clean          - Remove build artifacts
make assets-game    - Rebuild all sprite PNGs from the game's SLP files
make assets         - Extract a single SLP (SLP=... OUT=... args)
make sprites        - Preprocess sprites/*.png + terrain → data/*.bin
```
