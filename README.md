# AoE2 DSi

Age of Empires 2 port for Nintendo DSi.

## Build

```bash
# Set up devkitPro (add to ~/.zshrc)
export DEVKITPRO=/opt/devkitpro
export DEVKITARM=$DEVKITPRO/devkitARM

# Build
cd aoe2_dsi
make
```

## Run

The resulting `.nds` file can be run in:
- [melonDS](https://melonds.kuribo64.net/) emulator
- On real hardware via a flash cart

## Controls

- D-pad: Move units
- Map is on the **bottom screen**; villager sprites render on the top screen.
- Touch: Tap villager on the map to select/deselect
- Touch: Tap ground to move (when not selected)
- Touch: Tap ground to build (when selected)
- A 1px marker shows the villager/building position on the map.
- When selected, a ghost marker follows the stylus for build preview.
- SELECT: Toggle camera follow
- L/R: Scroll background (Y axis) when camera follow is off
- A/Y: Scroll background (X axis) when camera follow is off
- START: Exit

## Assets

- Sample sprites from devkitPro (`sprites/man.png`, `woman.png`)
- Original AoE2:DE assets available in: `C:\Program Files (x86)\Steam\steamapps\common\AoE2DE`

## Asset Pipeline (HD / SLP)

This project can extract AoE2 HD (`Age2HD`) `.slp` sprites and pack them into NDS-ready sprite sheets.

### 1) Extract frames from an SLP

```bash
# Example: extract frame 0 from 2.slp using the HD palette
node extract-slp.js \\
  "/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics/2.slp" \\
  test_output \\
  0 \\
  "/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/pal_5.pal"
```

Omit the frame index to export all frames.

### 2) Pack frames into a sprite sheet

```bash
# Pack extracted frames into a 32x32 grid sprite sheet
python3 scripts/pack_frames.py test_output sprites/villager.png --cell 32x32 --cols 4 --fit
```

Notes:
- `--fit` scales frames down to fit within the cell.
- The output sheet can be used directly by `grit` via the existing Makefile rule.

### 3) One-shot extract + pack

```bash
python3 scripts/build_sprite_sheet.py \\
  "/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics/2.slp" \\
  sprites/villager.png \\
  --palette "/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/pal_5.pal" \\
  --cell 32x32 \\
  --cols 4 \\
  --fit
```

Or use the Makefile helper:

```bash
make assets \\
  SLP="/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/drs/graphics/2.slp" \\
  OUT="sprites/villager.png" \\
  PALETTE="/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/dat/pal_5.pal" \\
  CELL=32x32 \\
  COLS=4 \\
  FIT=1
```

### 4) Batch build via manifest

Edit `assets/manifest.json` and then run:

```bash
make assets-batch
```

You can auto-generate a starter HD manifest and build it:

```bash
python3 scripts/gen_manifest_hd.py
make assets-batch-hd
```

Generate a quick preview page for the generated sheets:

```bash
python3 scripts/gen_sprite_index.py
```

Open `sprites/index.html` in a browser to review the sheets.

## Development

See [PLAN.md](./PLAN.md) for the development roadmap.

## Notes / Learnings

- **Grass background**: Use a bitmap BG (BG2) with `BgType_Bmp8` and put it in VRAM bank A (`VRAM_A_MAIN_BG`). Sprites must be moved to a different bank (e.g. `VRAM_B_MAIN_SPRITE`) or the BG will be black.
- **Grass pipeline**: `sprites/grass.png` is generated from HD terrain textures in `Age2HD/resources/_common/terrain/textures/`. See `scripts/make_grass_from_texture.py`.
- **Villager frame order**: Packed sheets are treated as a 4x3 grid (columns = directions, rows = frames). Animation index = `dir + frame * 4`.
- **Terrain variety**: `sprites/dirt.png` is generated from `g_des_00_color.png` via `scripts/make_texture_from_hd.py` and stamped onto the bitmap.
