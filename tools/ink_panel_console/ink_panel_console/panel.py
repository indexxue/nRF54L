"""4.2" BWR plane helpers (WS 4.2B V2：400×300，flip_y=False)."""

from __future__ import annotations

from pathlib import Path

from .paths import ensure_nrf_console_on_path

ensure_nrf_console_on_path()

from nrf_console.epd.plane import (  # noqa: E402
    PANEL_4IN2,
    Color,
    PanelProfile,
    empty_planes,
)

PANEL: PanelProfile = PANEL_4IN2


def make_empty() -> tuple[bytearray, bytearray]:
    return empty_planes(panel=PANEL)


def set_pixel(bw: bytearray, red: bytearray, x: int, y: int, color: Color) -> None:
    w, h = PANEL.width, PANEL.height
    if not (0 <= x < w and 0 <= y < h):
        return
    row_bytes = PANEL.row_bytes
    idx = y * row_bytes + (x // 8)
    bit = 7 - (x % 8)
    mask = 1 << bit
    if color == Color.WHITE:
        bw[idx] |= mask
        red[idx] &= ~mask & 0xFF
    elif color == Color.BLACK:
        bw[idx] &= ~mask & 0xFF
        red[idx] &= ~mask & 0xFF
    elif color == Color.RED:
        bw[idx] |= mask
        red[idx] |= mask
    else:
        raise ValueError(color)


def fill_rect(
    bw: bytearray,
    red: bytearray,
    x0: int,
    y0: int,
    x1: int,
    y1: int,
    color: Color,
) -> None:
    w, h = PANEL.width, PANEL.height
    x0 = max(0, min(x0, w - 1))
    y0 = max(0, min(y0, h - 1))
    x1 = max(0, min(x1, w - 1))
    y1 = max(0, min(y1, h - 1))
    if x0 > x1:
        x0, x1 = x1, x0
    if y0 > y1:
        y0, y1 = y1, y0
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            set_pixel(bw, red, x, y, color)


def image_to_planes(
    path: str | Path,
    *,
    luminance_threshold: int = 140,
    red_sat_min: int = 40,
    red_dom_min: int = 30,
) -> tuple[bytes, bytes]:
    """PNG/BMP/JPEG → 400×300 BWR（contain + 居中）。"""
    try:
        from PIL import Image
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("Pillow required — pip install Pillow") from exc

    src = Image.open(path).convert("RGB")
    tw, th = PANEL.width, PANEL.height
    iw, ih = src.size
    scale = min(tw / iw, th / ih)
    nw = max(1, int(iw * scale))
    nh = max(1, int(ih * scale))
    fitted = src.resize((nw, nh), Image.Resampling.LANCZOS)
    bw, red = make_empty()
    ox = (tw - nw) // 2
    oy = (th - nh) // 2
    pixels = fitted.load()
    assert pixels is not None
    for y in range(nh):
        for x in range(nw):
            r, g, b = pixels[x, y]
            lum = (r * 3 + g * 6 + b) // 10
            sat = max(r, g, b) - min(r, g, b)
            if r >= g + red_dom_min and r >= b + red_dom_min and sat >= red_sat_min:
                color = Color.RED
            elif lum < luminance_threshold:
                color = Color.BLACK
            else:
                color = Color.WHITE
            set_pixel(bw, red, ox + x, oy + y, color)
    return bytes(bw), bytes(red)
