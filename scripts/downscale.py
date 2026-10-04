"""Exact 3:1 reduction of AoE2 sprites.

AoE2 tiles are 96x48 pixels and ours are 32x16, so every sprite is reduced by
exactly 3 — no fitting, no per-sprite scale. Two rules keep the result crisp:

- The SLP hotspot stays on a pixel boundary: blocks are laid out from the
  hotspot outward, so it maps to an exact output coordinate (the anchor).
- Pixels are picked, not blended: each 3x3 block becomes the source pixel
  closest to the block's average colour, so no new colours appear and edges
  stay hard. A block is solid when most of it (5 of 9) is.
"""
import numpy as np

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
