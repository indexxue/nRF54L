"""Host-side EPD plane helpers (GDEY029Z95 RAM layout)."""

from __future__ import annotations

from enum import IntEnum

WIDTH = 128
HEIGHT = 296
ROW_BYTES = 16  # 128 / 8
PLANE_SIZE = ROW_BYTES * HEIGHT  # 4736
FLIP_Y = True  # matches epd_panel_gdey029z95.flip_y


class Color(IntEnum):
    WHITE = 0
    BLACK = 1
    RED = 2


def empty_planes(*, white_bw: bool = True) -> tuple[bytearray, bytearray]:
    """BW default white (0xFF); RED default clear (0x00)."""
    bw = bytearray(b"\xFF" * PLANE_SIZE if white_bw else b"\x00" * PLANE_SIZE)
    red = bytearray(PLANE_SIZE)
    return bw, red


def _ram_y(logical_y: int) -> int:
    if FLIP_Y:
        return (HEIGHT - 1) - logical_y
    return logical_y


def _byte_index(x: int, logical_y: int) -> tuple[int, int]:
    """Return (plane_index, bit) where bit 7 is leftmost pixel in the byte."""
    if not (0 <= x < WIDTH and 0 <= logical_y < HEIGHT):
        raise ValueError(f"pixel out of range: ({x},{logical_y})")
    ry = _ram_y(logical_y)
    idx = ry * ROW_BYTES + (x // 8)
    bit = 7 - (x % 8)
    return idx, bit


def set_pixel(bw: bytearray, red: bytearray, x: int, y: int, color: Color) -> None:
    """Pack one logical pixel into MCU planes (1=white on BW, 1=red on RED)."""
    idx, bit = _byte_index(x, y)
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
        raise ValueError(f"unknown color: {color}")


def fill_rect(
    bw: bytearray,
    red: bytearray,
    x0: int,
    y0: int,
    x1: int,
    y1: int,
    color: Color,
) -> None:
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            set_pixel(bw, red, x, y, color)


def make_smoke_bwr() -> tuple[bytes, bytes]:
    """
    Simple visual smoke: three vertical bands (black / red / white) + border.
    Easy to confirm on GDEY029Z95 after BLE upload.
    """
    bw, red = empty_planes()
    col_w = WIDTH // 3
    fill_rect(bw, red, 0, 0, col_w - 1, HEIGHT - 1, Color.BLACK)
    fill_rect(bw, red, col_w, 0, col_w * 2 - 1, HEIGHT - 1, Color.RED)
    fill_rect(bw, red, col_w * 2, 0, WIDTH - 1, HEIGHT - 1, Color.WHITE)
    # 1px black border
    fill_rect(bw, red, 0, 0, WIDTH - 1, 0, Color.BLACK)
    fill_rect(bw, red, 0, HEIGHT - 1, WIDTH - 1, HEIGHT - 1, Color.BLACK)
    fill_rect(bw, red, 0, 0, 0, HEIGHT - 1, Color.BLACK)
    fill_rect(bw, red, WIDTH - 1, 0, WIDTH - 1, HEIGHT - 1, Color.BLACK)
    return bytes(bw), bytes(red)
