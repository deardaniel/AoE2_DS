"""Shared constants for NDS preprocessing scripts.

These values MUST match the C++ definitions in source/config.h and source/iso.h.
If you change them here, update the C++ headers too (and vice versa).

Canonical C++ locations:
  ISO_TILE_W, ISO_TILE_H, TILE_PX  → source/config.h
  ISO_DIAMOND_XSTART/XEND          → source/iso.h
  PAL_* indices                     → source/config.h (enum PaletteIndex)
  TERRAIN_COUNT, GRASS_VARIANTS     → source/config.h
  footH formula                     → source/render.cpp (bldg_sprite_offset)
"""

# ---------------------------------------------------------------------------
# Isometric tile geometry
# ---------------------------------------------------------------------------
ISO_TILE_W = 32   # Isometric tile width in pixels
ISO_TILE_H = 16   # Isometric tile height in pixels
TILE_PX    = 16   # World-space tile size in pixels

# Diamond mask: for each row of an ISO_TILE_H-high tile, the X range of
# filled pixels. Must match ISO_DIAMOND_XSTART/XEND in source/iso.h.
ISO_DIAMOND_XSTART = [15, 13, 11, 9, 7, 5, 3, 1, 1, 3, 5, 7, 9, 11, 13, 15]
ISO_DIAMOND_XEND   = [16, 18, 20, 22, 24, 26, 28, 30, 30, 28, 26, 24, 22, 20, 18, 16]

# ---------------------------------------------------------------------------
# Terrain
# ---------------------------------------------------------------------------
TERRAIN_COUNT  = 8   # Number of base terrain types (GRASS..BERRIES)
GRASS_VARIANTS = 16  # Number of grass tile visual variants
DIRT_VARIANTS  = 4   # Number of dirt tile visual variants

# ---------------------------------------------------------------------------
# UI palette (indices 0-15 in BG_PALETTE_SUB)
# Must match enum PaletteIndex in source/config.h
# ---------------------------------------------------------------------------
PAL_TRANSPARENT = 0
PAL_BLACK       = 1
PAL_BROWN       = 2
PAL_TAN         = 3
PAL_BLUE        = 4
PAL_RED         = 5
PAL_DARKGREEN   = 6
PAL_GREEN       = 7
PAL_YELLOW      = 8
PAL_GRAY        = 9
PAL_DARKGRAY    = 10
PAL_WHITE       = 11
PAL_ORANGE      = 12
PAL_LIGHTBROWN  = 13
PAL_SKIN        = 14
PAL_DARKBROWN   = 15

# RGB888 values for each UI palette slot
UI_PALETTE_RGB = {
    PAL_BLACK:      (0,   0,   0),
    PAL_BROWN:      (80,  48,  16),
    PAL_TAN:        (160, 128, 80),
    PAL_BLUE:       (32,  64,  224),
    PAL_RED:        (224, 32,  32),
    PAL_DARKGREEN:  (16,  96,  16),
    PAL_GREEN:      (32,  160, 32),
    PAL_YELLOW:     (224, 224, 32),
    PAL_GRAY:       (128, 128, 128),
    PAL_DARKGRAY:   (64,  64,  64),
    PAL_WHITE:      (248, 248, 248),
    PAL_ORANGE:     (224, 128, 32),
    PAL_LIGHTBROWN: (176, 144, 80),
    PAL_SKIN:       (224, 176, 128),
    PAL_DARKBROWN:  (48,  32,  16),
}

# ---------------------------------------------------------------------------
# Town Center composite (scripts/composite_tc.py)
# Must match TC_SPR_W/H and TC_ANCHOR_X/Y in source/render.cpp
# ---------------------------------------------------------------------------
TC_CANVAS = (136, 80)  # sprite canvas W x H
TC_ANCHOR = (68, 62)   # canvas pixel of the footprint diamond's centre

# ---------------------------------------------------------------------------
# Unit sprite sheets (scripts/build_unit_sheets.py)
# ---------------------------------------------------------------------------
UNIT_CELL   = (32, 32)  # CELL_W x CELL_H in source/render.cpp
# Cell pixel where the unit's SLP hotspot (its ground position) goes. Must
# match UNIT_ANCHOR_X/Y in source/render.cpp, which puts it on the tile centre.
UNIT_ANCHOR = (16, 24)
# AoE2 pixels -> NDS pixels: the same 1/3 as the terrain (96x48 -> 32x16 tiles)
# and the Town Center, so units are in proportion to both. Cavalry and siege
# come out a few percent smaller (see group_scale in build_unit_sheets.py).
UNIT_SCALE  = 1 / 3

# Ground shadow: sprite PNGs store it as black at this alpha, the indexed
# data as PAL_SHADOW (SPR_SHADOW in source/render.cpp), which is drawn by
# darkening the terrain underneath.
SHADOW_ALPHA = 96
PAL_SHADOW   = 1

# ---------------------------------------------------------------------------
# Isometric helpers
# ---------------------------------------------------------------------------

def footprint_height(tile_w, tile_h):
    """Isometric footprint height in pixels for a tile_w × tile_h building.

    Must match: int footH = (tileW + tileH) * (ISO_TILE_H / 2) in render.cpp.
    """
    return (tile_w + tile_h) * (ISO_TILE_H // 2)


def footprint_width(tile_w, tile_h):
    """Isometric footprint width in pixels for a tile_w × tile_h building."""
    return (tile_w + tile_h) * (ISO_TILE_W // 2)


def rgb_to_bgr555(r, g, b):
    """Convert RGB888 to NDS BGR555 format (16-bit)."""
    r5 = min(31, r >> 3)
    g5 = min(31, g >> 3)
    b5 = min(31, b >> 3)
    return (b5 << 10) | (g5 << 5) | r5 | (1 << 15)


def bgr555_to_rgb(val):
    """Convert NDS BGR555 to RGB888."""
    r = (val & 0x1F) << 3
    g = ((val >> 5) & 0x1F) << 3
    b = ((val >> 10) & 0x1F) << 3
    return (r, g, b)
