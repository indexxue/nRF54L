"""Plane helpers when importing from HTML designer (logical top-left rows)."""

from __future__ import annotations


def flip_plane_y(data: bytes, width: int, height: int) -> bytes:
    """Map logical row 0 (top) -> RAM row 0 (bottom) for flip_y panels."""
    row = (width + 7) // 8
    expect = row * height
    if len(data) != expect:
        raise ValueError(f"plane len {len(data)} != {expect} ({width}x{height})")
    out = bytearray(expect)
    for y in range(height):
        src = y * row
        dst = (height - 1 - y) * row
        out[dst : dst + row] = data[src : src + row]
    return bytes(out)
