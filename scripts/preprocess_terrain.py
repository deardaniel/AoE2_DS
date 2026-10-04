#!/usr/bin/env python3
"""Build the terrain tiles from the AoE2 HD ground textures.

The game does not draw a tile as a patch of texture: each terrain has one
512x512 texture that lies flat on the ground, repeats every 10 tiles, and is
seen through the isometric projection. A tile at (tx, ty) shows the part of
the texture under it, so neighbouring tiles of the same terrain continue each
other with no seam.

This script does the same at our scale. For every position in the 10x10
pattern it renders the 32x16 tile by projecting each screen pixel back onto
the flat texture (51.2 texels per tile) and averaging the texels it covers.
The whole 32x16 rectangle is rendered, not just the diamond, so the pixels a
tile shares with its neighbours are correct too.

Colours are matched to the sprite palette (data/sprite_pal.bin, the game's
own palette), with a small ordered dither so smooth ground doesn't band.

Output:
  data/terrain_tiles.bin  TERRAIN_SETS x 100 tiles x 512 bytes; tile index is
                          set * 100 + (tx % 10) * 10 + (ty % 10)
  data/terrain_pal.bin    UI colours for palette indices 1-15

Requires: data/sprite_pal.bin (run preprocess_sprites.py first — `make
sprites` does both in order).
"""
import os
import struct
import sys

import numpy as np
from PIL import Image

from shared_constants import (
    ISO_TILE_W, ISO_TILE_H, TERRAIN_SETS, TERRAIN_PATTERN, UI_PALETTE_RGB,
    rgb_to_bgr555, bgr555_to_rgb,
)

DATA_DIR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'data')
TERRAIN_DIR = '/mnt/c/Program Files (x86)/Steam/steamapps/common/Age2HD/resources/_common/terrain/textures'

SUPERSAMPLE = 4          # samples per screen pixel, each way
DITHER_AMPLITUDE = 6     # +/- in 8-bit colour, before palette matching
BAYER4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]) / 16.0 - 0.5


def render_set(texture):
    """Render the 10x10 pattern of 32x16 tiles for one texture (RGB float)."""
    size = texture.shape[0]
    per_tile = size / TERRAIN_PATTERN
    s = SUPERSAMPLE
    # Sample positions inside the tile rectangle, relative to the diamond's
    # top vertex at x = 16
    xs = (np.arange(ISO_TILE_W * s) + 0.5) / s - ISO_TILE_W / 2
    ys = (np.arange(ISO_TILE_H * s) + 0.5) / s
    x, y = np.meshgrid(xs, ys)
    # Inverse of screen = ((tx - ty) * 16, (tx + ty) * 8)
    u = (x / (ISO_TILE_W / 2) + y / (ISO_TILE_H / 2)) / 2
    v = (y / (ISO_TILE_H / 2) - x / (ISO_TILE_W / 2)) / 2

    tiles = np.zeros((TERRAIN_PATTERN, TERRAIN_PATTERN, ISO_TILE_H, ISO_TILE_W, 3))
    for i in range(TERRAIN_PATTERN):
        for j in range(TERRAIN_PATTERN):
            tx = np.floor((i + u) * per_tile).astype(int) % size
            ty = np.floor((j + v) * per_tile).astype(int) % size
            samples = texture[ty, tx]
            tiles[i, j] = samples.reshape(ISO_TILE_H, s, ISO_TILE_W, s, 3).mean(axis=(1, 3))
    return tiles


def main():
    sprite_pal_path = os.path.join(DATA_DIR, 'sprite_pal.bin')
    if not os.path.exists(sprite_pal_path):
        sys.exit(f"{sprite_pal_path} not found. Run preprocess_sprites.py first.")
    with open(sprite_pal_path, 'rb') as f:
        pal_data = f.read()

    # Palette entries terrain may use: 16-255, minus the two player colour
    # ramps so ground never changes with the player remap
    indices = [i for i in range(16, 256) if not (16 <= i < 24 or 32 <= i < 40)]
    colours = np.array([bgr555_to_rgb(struct.unpack_from('<H', pal_data, i * 2)[0]) for i in indices],
                       dtype=np.float64)
    indices = np.array(indices, dtype=np.uint8)

    print("=== NDS Terrain Tile Preprocessor (Isometric 32x16) ===")
    out = bytearray()
    for name, filename in TERRAIN_SETS:
        path = os.path.join(TERRAIN_DIR, filename)
        if not os.path.exists(path):
            sys.exit(f"terrain texture not found: {path}")
        texture = np.asarray(Image.open(path).convert('RGB'), dtype=np.float64)
        tiles = render_set(texture)
        for i in range(TERRAIN_PATTERN):
            for j in range(TERRAIN_PATTERN):
                rgb = tiles[i, j]
                # Ordered dither, continuous across tiles (keyed on screen position)
                yy, xx = np.indices((ISO_TILE_H, ISO_TILE_W))
                sx = (i - j) * (ISO_TILE_W // 2) + xx
                sy = (i + j) * (ISO_TILE_H // 2) + yy
                rgb = rgb + (BAYER4[sy % 4, sx % 4] * 2 * DITHER_AMPLITUDE)[:, :, None]
                flat = rgb.reshape(-1, 1, 3)
                nearest = ((flat - colours[None, :, :]) ** 2).sum(axis=2).argmin(axis=1)
                out.extend(indices[nearest].tobytes())
        print(f"  {name}: {filename}, {TERRAIN_PATTERN * TERRAIN_PATTERN} tiles")

    tiles_path = os.path.join(DATA_DIR, 'terrain_tiles.bin')
    with open(tiles_path, 'wb') as f:
        f.write(out)
    print(f"  {tiles_path}: {len(out)} bytes")

    # UI colours for palette indices 1-15 (loaded over the sprite palette at runtime)
    pal_bin = bytearray(512)
    struct.pack_into('<H', pal_bin, 0, rgb_to_bgr555(0, 0, 0))
    for idx, (r, g, b) in UI_PALETTE_RGB.items():
        struct.pack_into('<H', pal_bin, idx * 2, rgb_to_bgr555(r, g, b))
    pal_path = os.path.join(DATA_DIR, 'terrain_pal.bin')
    with open(pal_path, 'wb') as f:
        f.write(pal_bin)
    print(f"  {pal_path}: {len(pal_bin)} bytes (UI colors at indices 1-15)")


if __name__ == '__main__':
    main()
