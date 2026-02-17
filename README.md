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
- START: Exit

## Assets

- Sample sprites from devkitPro (`sprites/man.png`, `woman.png`)
- Original AoE2:DE assets available in: `C:\Program Files (x86)\Steam\steamapps\common\AoE2DE`

## Development

See [PLAN.md](./PLAN.md) for the development roadmap.
