"""Exact 3:1 reduction of AoE2 sprites.

AoE2 tiles are 96x48 pixels and ours are 32x16, so every sprite is reduced by
exactly 3 — no fitting, no per-sprite scale. Two rules keep the result crisp:

- The SLP hotspot stays on a pixel boundary: blocks are laid out from the
  hotspot outward, so it maps to an exact output coordinate (the anchor).
- Pixels are picked, not blended: each 3x3 block becomes the source pixel
  closest to the block's average colour, so no new colours appear and edges
  stay hard. A block is solid when most of it (5 of 9) is.

Shadows: genie-slp draws SLP shadow commands as pure red. reduce_layers()
turns those into black pixels at SHADOW_ALPHA, which preprocess_sprites.py
indexes as the marker the renderer darkens terrain with.
"""
import numpy as np

from shared_constants import SHADOW_ALPHA

SCALE = 3


def reduce_exact(rgba, hx, hy, scale=SCALE):
    """Reduce an RGBA array (alpha 0/255) by `scale` around its hotspot.

    Returns (out, ax, ay): the reduced RGBA array cropped to its content, and
    the hotspot's position in it (may lie outside the array).
    """
    h, w = rgba.shape[:2]
    # Block-aligned bounds relative to the hotspot
    bx0, by0 = (0 - hx) // scale, (0 - hy) // scale
    bx1, by1 = -(-(w - hx) // scale), -(-(h - hy) // scale)
    bw, bh = bx1 - bx0, by1 - by0
    padded = np.zeros((bh * scale, bw * scale, 4), dtype=np.uint8)
    ox, oy = -hx - bx0 * scale, -hy - by0 * scale
    padded[oy:oy + h, ox:ox + w] = rgba

    blocks = padded.reshape(bh, scale, bw, scale, 4).transpose(0, 2, 1, 3, 4)
    blocks = blocks.reshape(bh, bw, scale * scale, 4).astype(np.float64)
    solid = blocks[..., 3] > 0
    cover = solid.sum(-1)
    mean = (blocks[..., :3] * solid[..., None]).sum(2) / np.maximum(cover, 1)[..., None]
    dist = ((blocks[..., :3] - mean[:, :, None, :]) ** 2).sum(-1)
    dist[~solid] = np.inf
    pick = dist.argmin(-1)
    ii, jj = np.indices(pick.shape)

    out = np.zeros((bh, bw, 4), dtype=np.uint8)
    keep = cover * 2 > scale * scale
    out[keep, :3] = blocks[ii, jj, pick][keep][:, :3]
    out[keep, 3] = 255

    ax, ay = -bx0, -by0
    ys, xs = np.nonzero(keep)
    if len(xs) == 0:
        return out[:1, :1], ax, ay
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    return out[y0:y1, x0:x1], ax - x0, ay - y0


def _shadow_mask(rgba):
    return (rgba[:, :, 3] > 0) & (rgba[:, :, 0] == 255) & (rgba[:, :, 1] == 0) & (rgba[:, :, 2] == 0)


def reduce_shadow(rgba, hx, hy, scale=SCALE):
    """Reduce a frame's shadow pixels to a boolean mask and its anchor.

    A block is shadow when most of it is.
    """
    h, w = rgba.shape[:2]
    bx0, by0 = (0 - hx) // scale, (0 - hy) // scale
    bx1, by1 = -(-(w - hx) // scale), -(-(h - hy) // scale)
    padded = np.zeros(((by1 - by0) * scale, (bx1 - bx0) * scale), dtype=np.int32)
    ox, oy = -hx - bx0 * scale, -hy - by0 * scale
    padded[oy:oy + h, ox:ox + w] = _shadow_mask(rgba)
    cover = padded.reshape(by1 - by0, scale, bx1 - bx0, scale).sum(axis=(1, 3))
    return cover * 2 > scale * scale, -bx0, -by0


def reduce_layers(layers, scale=SCALE):
    """Reduce a stack of frames, bottom to top, into one sprite.

    layers: [(RGBA array, hotspot_x, hotspot_y)], hotspots aligned. In each
    layer the shadow pixels go underneath as SHADOW_ALPHA black and the rest
    is reduced with reduce_exact. Returns (RGBA array, anchor_x, anchor_y).
    """
    shades, solids = [], []
    for rgba, hx, hy in layers:
        is_shadow = _shadow_mask(rgba)
        if is_shadow.any():
            mask, ax, ay = reduce_shadow(rgba, hx, hy, scale)
            shade = np.zeros(mask.shape + (4,), dtype=np.uint8)
            shade[mask] = (0, 0, 0, SHADOW_ALPHA)
            shades.append((shade, ax, ay))
        solid = rgba.copy()
        solid[is_shadow] = 0
        if solid[:, :, 3].any():
            solids.append(reduce_exact(solid, hx, hy, scale))
    parts = shades + solids
    left = max(ax for _, ax, _ in parts)
    up = max(ay for _, _, ay in parts)
    w = left + max(p.shape[1] - ax for p, ax, _ in parts)
    h = up + max(p.shape[0] - ay for p, _, ay in parts)
    out = np.zeros((h, w, 4), dtype=np.uint8)
    for p, ax, ay in parts:
        region = out[up - ay:up - ay + p.shape[0], left - ax:left - ax + p.shape[1]]
        region[p[:, :, 3] > 0] = p[p[:, :, 3] > 0]
    return out, int(left), int(up)
