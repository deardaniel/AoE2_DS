# AoE2 DSi - Development Plan

## Constraints (DSi/NDS)
- **Screen**: 256×192 (top), 256×192 (bottom) - tiny!
- **RAM**: ~8MB total
- **Storage**: Cartridge (up to 128MB)
- **No original game assets** - we'll create simple graphics

## Architecture

```
┌─────────────────────────────────────┐
│           Game Loop                 │
│  (input → update → render)         │
└──────────────┬──────────────────────┘
               │
    ┌──────────┼──────────┐
    ▼          ▼          ▼
┌───────┐  ┌───────┐  ┌───────┐
│ Input │  │ Logic │  │Render │
│ Touch │  │ Units │  │Sprite │
│ Keys  │  │ AI    │  │TileMap│
└───────┘  └───────┘  └───────┘
```

## Phases

| Phase | Goal | Features |
|-------|------|----------|
| 1 | **Terrain** | Tile map, grass, roads, resources |
| 2 | **Units** | Villagers, soldiers (simple sprites) |
| 3 | **Buildings** | Town Center, barracks (tile-based) |
| 4 | **Economy** | Gather gold/wood/food |
| 5 | **Combat** | Units attack each other |
| 6 | **AI** | Simple enemy AI |

## Key Technical Decisions

1. **Graphics**: 8-bit indexed color (like our working demo)
2. **Sprites**: OAM-based for units (hardware accelerated)
3. **Map**: Tile-based (16×12 tiles visible on screen)
4. **Input**: Touch + buttons (D-pad for menu)
5. **No original assets** - we'll draw simple pixel art

## File Structure
```
aoe2_dsi/
├── source/
│   ├── main.cpp       # Entry point
│   ├── graphics.cpp   # Sprite/tile rendering
│   ├── game.cpp       # Game logic
│   ├── units.cpp      # Unit definitions
│   └── input.cpp      # Touch/button handling
├── data/
│   ├── tiles.png      # Terrain tiles (grass, water, etc)
│   └── sprites.png    # Unit sprites
└── Makefile
```

## Progress
- [x] Graphics rendering working (green terrain on top, console on bottom)
- [ ] Phase 1: Terrain system

## Assets

### Original AoE2: DE (on this machine)
- Location: `C:\Program Files (x86)\Steam\steamapps\common\AoE2DE`
- **Terrain textures**: `resources/_common/terrain/textures/2x/*.dds` (40+ files)
  - g_gr1-gr9 = grass variants, g_for = forest, g_bch = beach, g_des = desert
- **Sprite archives**: `resources/_common/drs/graphics/*.slp` (35 files)
  - game_b*.slp = building sprites
  - Unit sprites likely in gamedata_x2
- **Data files**: `resources/_common/dat/*.dat` - unit/building definitions

### NDS DevkitPro Examples (for reference)
- `/opt/devkitpro/examples/nds/Graphics/Sprites/animate_simple/` - **perfect for unit sprites!**
- Walking animation with 4 directions, 3 frames each
- Shows both VRAM-saving and fast-animation approaches
