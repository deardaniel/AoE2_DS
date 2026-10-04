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

### Rebuild the sprite PNGs from the game files
```bash
make assets-game   # composite_tc.py, extract_buildings.py, build_unit_sheets.py
```
Each script owns the table of SLP IDs for its sprites. The IDs come from the
game's `.dat`, looked up **by ID** (`node scripts/dump_unit_graphics.js <unit>`);
see `docs/building_graphics_reference.md` before adding or changing one.

Everything is reduced by exactly 3 (AoE2 tiles are 96x48, ours 32x16) around
its SLP hotspot, picking source pixels rather than blending them
(`scripts/downscale.py`). Nothing is fitted or scaled per sprite.

- Town Center: composited from its nine SLP layers
- Buildings: raw SLP frame plus a hotspot sidecar; the hotspot sits on the
  centre of the building's real footprint. Construction sites come from the
  game's staged CNSTn_NN graphics
- Units: each sheet gets the smallest cell that holds its frames, hotspot on
  the tile centre

`make sprites` writes the resulting sizes and anchors to
`source/sprite_geom.h`, which the renderer reads — none are typed in by hand.

`node extract-slp.js <path-to-slp> <output-dir> [frame-index]` dumps the
frames of a single SLP for inspection.

### Preprocess for NDS
```bash
make sprites  # runs preprocess_sprites.py then preprocess_terrain.py
```

This generates:
- `data/sprite_pal.bin` — shared 256-color BGR555 palette
- `data/sprite_remap.bin` — player color remap table (blue → red)
- `data/spr_*.bin` — indexed pixel data for each sprite sheet
- `data/terrain_*.bin` — terrain tile graphics and palette

The scripts need numpy and Pillow (`python3 -m venv --system-site-packages .venv
&& .venv/bin/pip install numpy`, then put `.venv/bin` first on `PATH`).

The ARM9 image must fit the 2.6 MB `lma9` region; it is about 2.0 MB now.

### Checking sprites in the emulator
```bash
make SHOWCASE=1   # the smaller buildings beside the player's Town Center
make SHOWCASE=2   # one of every unit, turning every 2 seconds
make SHOWCASE=3   # normal start with a villager scripted onto a sheep
make SHOWCASE=4   # the 4x4 buildings and each construction stage
```
`touch source/main.cpp` when switching between these and a normal build.
`tools/emu/emu.sh` launches a private melonDS, takes screenshots and sends
taps, drags and key presses to it.

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
