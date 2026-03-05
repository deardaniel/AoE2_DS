#!/usr/bin/env python3
"""Convert icon.bmp to 16-color paletted BMP with ordered (Bayer) dithering."""
import sys
from PIL import Image
import numpy as np

src = sys.argv[1] if len(sys.argv) > 1 else 'icon.bmp'
dst = sys.argv[2] if len(sys.argv) > 2 else 'build_icon.bmp'

img = Image.open(src).convert('RGB')

# Build optimal 16-color palette via median cut
q = img.quantize(colors=16, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)

# Apply 4x4 Bayer ordered dithering
px = np.array(img, dtype=np.float32)
bayer = np.array([
    [ 0, 8, 2,10],
    [12, 4,14, 6],
    [ 3,11, 1, 9],
    [15, 7,13, 5],
], dtype=np.float32) / 16.0 * 32 - 16

h, w = px.shape[:2]
bt = np.tile(bayer, (h // 4 + 1, w // 4 + 1))[:h, :w]
for c in range(3):
    px[:, :, c] = np.clip(px[:, :, c] + bt, 0, 255)

# Quantize the dithered image using the same palette
result = Image.fromarray(px.astype(np.uint8)).quantize(
    colors=16, palette=q, dither=Image.Dither.NONE)
result.save(dst)
