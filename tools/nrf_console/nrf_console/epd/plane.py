"""Host-side EPD plane helpers (panel profiles + pixel pack)."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum


@dataclass(frozen=True)
class PanelProfile:
    """Matches firmware epd_panel_* geometry / flip_y."""

    name: str
    width: int
    height: int
    flip_y: bool

    @property
    def row_bytes(self) -> int:
        return (self.width + 7) // 8

    @property
    def plane_size(self) -> int:
        return self.row_bytes * self.height


# Firmware-supported panels (eink_tag NVS epanel)
PANEL_2IN9 = PanelProfile("GDEY029Z95", 128, 296, flip_y=True)
PANEL_4IN2 = PanelProfile("WS_4IN2B_V2", 400, 300, flip_y=False)

PANEL_PROFILES: tuple[PanelProfile, ...] = (PANEL_2IN9, PANEL_4IN2)

# Default = 2.9" (Tk editor / smoke pages)
WIDTH = PANEL_2IN9.width
HEIGHT = PANEL_2IN9.height
ROW_BYTES = PANEL_2IN9.row_bytes
PLANE_SIZE = PANEL_2IN9.plane_size
FLIP_Y = PANEL_2IN9.flip_y


def panel_by_size(width: int, height: int) -> PanelProfile | None:
    for p in PANEL_PROFILES:
        if p.width == width and p.height == height:
            return p
    return None


def panel_by_plane_size(plane_size: int) -> PanelProfile | None:
    for p in PANEL_PROFILES:
        if p.plane_size == plane_size:
            return p
    return None


def plane_byte_count(width: int, height: int) -> int:
    if width < 1 or height < 1:
        raise ValueError(f"bad size {width}x{height}")
    return ((width + 7) // 8) * height


def geometry_for(width: int, height: int) -> PanelProfile:
    """
    Build upload geometry for any canvas size.
    Known firmware panels supply flip_y; unknown sizes default flip_y=False.
    Size match vs device is enforced by firmware DISP_BEGIN.
    """
    known = panel_by_size(width, height)
    if known is not None:
        return known
    plane = plane_byte_count(width, height)
    # Protocol: bw_len / red_len / chunk offset are u16
    if plane > 0xFFFF:
        raise ValueError(f"plane {plane} B exceeds u16 protocol limit ({width}x{height})")
    return PanelProfile(f"{width}x{height}", width, height, flip_y=False)


def require_panel(width: int, height: int) -> PanelProfile:
    """Alias of geometry_for (any size; firmware rejects mismatch)."""
    return geometry_for(width, height)


class Color(IntEnum):
    WHITE = 0
    BLACK = 1
    RED = 2


def empty_planes(*, white_bw: bool = True, panel: PanelProfile | None = None) -> tuple[bytearray, bytearray]:
    """BW default white (0xFF); RED default clear (0x00)."""
    sz = (panel or PANEL_2IN9).plane_size
    bw = bytearray(b"\xFF" * sz if white_bw else b"\x00" * sz)
    red = bytearray(sz)
    return bw, red


def _ram_y(logical_y: int, *, height: int = HEIGHT, flip_y: bool = FLIP_Y) -> int:
    if flip_y:
        return (height - 1) - logical_y
    return logical_y


def _byte_index(
    x: int,
    logical_y: int,
    *,
    width: int = WIDTH,
    height: int = HEIGHT,
    flip_y: bool = FLIP_Y,
) -> tuple[int, int]:
    """Return (plane_index, bit) where bit 7 is leftmost pixel in the byte."""
    if not (0 <= x < width and 0 <= logical_y < height):
        raise ValueError(f"pixel out of range: ({x},{logical_y})")
    row_bytes = (width + 7) // 8
    ry = _ram_y(logical_y, height=height, flip_y=flip_y)
    idx = ry * row_bytes + (x // 8)
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
