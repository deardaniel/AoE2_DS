# AoE2 DSi

Age of Empires 2 demake for Nintendo DS/DSi. A from-scratch implementation featuring isometric terrain, resource gathering, building construction, unit combat, AI opponent, tech research, and age advancement — all running on NDS hardware.

## Screenshots

*(Run in melonDS emulator)*

## Features

- **Isometric terrain** with procedurally varied grass, dirt, forests, gold, stone, and berry bushes
- **Resource gathering** — villagers auto-gather wood, food, gold, stone; forage berries; herd sheep
- **Building construction** — Town Center, Houses, Barracks, Archery Range, Stable, Farms, Mining/Lumber Camps, Market, Towers, Walls, Castle
- **Unit training** — Villagers, Militia, Archers, Knights, Spearmen, Scouts
- **Age advancement** — Dark Age through Imperial Age with progressive unlocks
- **Technology research** — Man-at-Arms, Crossbow, Cavalier upgrades
- **AI opponent** — need-based economy, building priorities, army composition, attack decisions
- **Fog of war** — explored/unexplored/visible states per tile
- **Market trading** — sell resources at 70% exchange rate
- **Rally points** — set spawn destinations for trained units
- **TC garrison** — shelter villagers from attacks
- **Multi-unit selection** — drag-select and double-tap-to-select-all-of-type
- **Under attack alerts** — flashing warning text and minimap indicator
- **Background music** — streamed from NitroFS

## Build

Requires [devkitPro](https://devkitpro.org/) with devkitARM and libnds.

```bash
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM

make          # build ROM
make sprites  # regenerate sprite/terrain binaries (requires AoE2 HD assets)
```

## Run

The output `aoe2_dsi.nds` runs in:
- [melonDS](https://melonds.kuribo64.net/) emulator (recommended)
- Real NDS/DSi hardware via flash cart

## Controls

### Touch Screen (Bottom)
| Action | Input |
|--------|-------|
| Select unit/building | Tap on it |
| Move selected units | Tap ground |
| Gather resource | Tap resource tile with villager selected |
| Attack enemy | Tap enemy unit with military selected |
| Gather sheep | Tap own sheep with villager selected |
| Drag-select units | Touch and drag |
| Select all of type | Double-tap a selected unit |
| Set rally point | Tap map with building selected |
| Place building | Tap ground in build mode |

### Buttons
| Button | Action |
|--------|--------|
| D-pad | Scroll camera (double-tap direction for fast scroll) |
| D-pad Up/Down | Cycle train unit type (when military building selected) |
| D-pad Left/Right | Cycle market trade (when market selected) |
| SELECT | Toggle camera follow on selected unit |
| START | Train unit / research tech / age up (building selected) or toggle music |
| X | Open/cycle build menu pages |
| B | Cancel build mode / cancel training queue |
| A | Ungarrison TC / select first military unit |
| Y | Center camera on Town Center |
| L/R | Cycle to next/first idle villager |

### Top Screen
Minimap display with resource/unit info HUD.

## Asset Pipeline

Sprites are extracted from AoE2 HD (Steam) SLP files and preprocessed into NDS-ready indexed binary data.

### Extract SLP frames
```bash
node extract-slp.js <path-to-slp> <output-dir> [frame-index]
```

### Pack into sprite sheet
```bash
python3 scripts/pack_frames.py <frames-dir> <output.png> \
  --cell 32x32 --cols 10 --dirs 5 --fpd 10 --fit
```

**Note:** The `--fit` flag computes a uniform scale factor from the largest frame to fit all frames within cells. This may affect relative sprite sizes across different sheets — consider using `--scale` with an explicit factor for consistent sizing.

### Preprocess for NDS
```bash
make sprites  # runs preprocess_sprites.py then preprocess_terrain.py
```

This generates:
- `data/sprite_pal.bin` — shared 256-color BGR555 palette
- `data/sprite_remap.bin` — player color remap table (blue → red)
- `data/spr_*.bin` — indexed pixel data for each sprite sheet
- `data/terrain_*.bin` — terrain tile graphics and palette

### Batch extract via manifest
```bash
# Edit assets/manifest_game.json, then:
make assets-batch
```

## Architecture

| File | Purpose |
|------|---------|
| `source/main.cpp` | Game loop, phase management, rendering pipeline |
| `source/terrain.cpp` | Map generation, isometric tile rendering |
| `source/render.cpp` | Software sprite rendering, depth sorting |
| `source/units.cpp` | Unit AI, pathfinding, combat, gathering |
| `source/buildings.cpp` | Building placement, training, garrison |
| `source/input.cpp` | Touch/button input, drag-select, build menu |
| `source/ai.cpp` | AI economy, building, military decisions |
| `source/ui.cpp` | Top screen minimap + console HUD |
| `source/game.cpp` | Game state, selection helpers, win/lose |
| `source/fog.cpp` | Fog of war visibility |
| `source/tech.cpp` | Technology research and stat modifiers |
| `source/sound.cpp` | NitroFS music streaming |
| `source/font.cpp` | Bitmap font rendering |
| `source/config.h` | All constants, stat tables, palette indices |
| `source/iso.h` | Isometric coordinate conversions |
| `scripts/preprocess_sprites.py` | PNG → indexed binary sprite data |
| `scripts/preprocess_terrain.py` | Terrain tile generation from HD textures |
| `scripts/shared_constants.py` | Shared Python/C++ constants |

## Technical Notes

- **NDS VRAM does not support byte writes** — all bitmap rendering goes to a main RAM buffer (`terrainBuf`), then `dmaCopy()` to VRAM each frame
- Bottom screen uses 8-bit indexed bitmap mode (Mode 5, BG2)
- Top screen uses 16-bit bitmap for minimap + text console overlay
- Units and buildings are software-rendered into the bitmap buffer (not OAM sprites) for unlimited count and flexible depth sorting
- Isometric diamond tile rendering with per-pixel fog of war overlay
