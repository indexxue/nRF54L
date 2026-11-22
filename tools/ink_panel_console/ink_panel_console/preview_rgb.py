"""BW/RED planes → RGB888 preview (4.2" flip_y=False)."""

from __future__ import annotations

from .panel import PANEL


def planes_to_rgb(bw: bytes, red: bytes, *, scale: int = 1) -> bytes:
    if scale < 1:
        raise ValueError("scale must be >= 1")
    w, h = PANEL.width, PANEL.height
    row_bytes = PANEL.row_bytes
    if len(bw) < PANEL.plane_size or len(red) < PANEL.plane_size:
        raise ValueError("plane too short")
    out = bytearray(w * scale * h * scale * 3)
    for y in range(h):
        row_off = y * row_bytes
        for x in range(w):
            idx = row_off + (x // 8)
            bit = 7 - (x % 8)
            mask = 1 << bit
            if red[idx] & mask:
                r, g, b = 194, 59, 42  # product red
            elif bw[idx] & mask:
                r, g, b = 244, 239, 227  # paper
            else:
                r, g, b = 26, 26, 24
            for sy in range(scale):
                for sx in range(scale):
                    ox = x * scale + sx
                    oy = y * scale + sy
                    p = (oy * w * scale + ox) * 3
                    out[p] = r
                    out[p + 1] = g
                    out[p + 2] = b
    return bytes(out)
