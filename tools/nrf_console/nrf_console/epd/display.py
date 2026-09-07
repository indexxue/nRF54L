"""Host-side drawing canvas aligned with cbb/epd/epd_display.*."""

from __future__ import annotations

from enum import IntEnum

from nrf_console.epd.assets import GfxAsset, get_asset
from nrf_console.epd.font_data import EPD_FONT_ASC12, EPD_FONT_ASC16, EPD_FONT_ASC24
from nrf_console.epd.plane import (
    FLIP_Y,
    HEIGHT,
    PLANE_SIZE,
    ROW_BYTES,
    WIDTH,
    Color,
    empty_planes,
    set_pixel,
)


class FontSize(IntEnum):
    FONT_12 = 12
    FONT_16 = 16
    FONT_24 = 24


_FONTS: dict[FontSize, tuple[bytes, ...]] = {
    FontSize.FONT_12: EPD_FONT_ASC12,
    FontSize.FONT_16: EPD_FONT_ASC16,
    FontSize.FONT_24: EPD_FONT_ASC24,
}


class Canvas:
    """128×296 BWR frame buffer (logical origin top-left, flip_y into RAM)."""

    def __init__(self) -> None:
        self.bw, self.red = empty_planes()

    def clear(self, color: Color = Color.WHITE) -> None:
        if color == Color.WHITE:
            self.bw[:] = b"\xFF" * PLANE_SIZE
            self.red[:] = b"\x00" * PLANE_SIZE
        elif color == Color.BLACK:
            self.bw[:] = b"\x00" * PLANE_SIZE
            self.red[:] = b"\x00" * PLANE_SIZE
        elif color == Color.RED:
            self.bw[:] = b"\xFF" * PLANE_SIZE
            self.red[:] = b"\xFF" * PLANE_SIZE
        else:
            raise ValueError(color)

    def set_pixel(self, x: int, y: int, color: Color) -> None:
        if not (0 <= x < WIDTH and 0 <= y < HEIGHT):
            return
        set_pixel(self.bw, self.red, x, y, color)

    def fill_rect(self, x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
        x0 = max(0, min(x0, WIDTH - 1))
        y0 = max(0, min(y0, HEIGHT - 1))
        x1 = max(0, min(x1, WIDTH - 1))
        y1 = max(0, min(y1, HEIGHT - 1))
        if x0 > x1:
            x0, x1 = x1, x0
        if y0 > y1:
            y0, y1 = y1, y0
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                set_pixel(self.bw, self.red, x, y, color)

    def draw_line(self, x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
        dx = abs(x1 - x0)
        dy = abs(y1 - y0)
        sx = 1 if x0 < x1 else -1
        sy = 1 if y0 < y1 else -1
        err = dx - dy
        x, y = x0, y0
        while True:
            self.set_pixel(x, y, color)
            if x == x1 and y == y1:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x += sx
            if e2 < dx:
                err += dx
                y += sy

    def draw_rect(self, x0: int, y0: int, x1: int, y1: int, color: Color) -> None:
        self.draw_line(x0, y0, x1, y0, color)
        self.draw_line(x0, y1, x1, y1, color)
        self.draw_line(x0, y0, x0, y1, color)
        self.draw_line(x1, y0, x1, y1, color)

    def draw_bitmap(
        self,
        x: int,
        y: int,
        w: int,
        h: int,
        bitmap: bytes,
        fg: Color,
        bg: Color | None = None,
    ) -> None:
        row_bytes = (w + 7) // 8
        for row in range(h):
            for col in range(w):
                byte_idx = row * row_bytes + (col // 8)
                mask = 0x80 >> (col % 8)
                on = (bitmap[byte_idx] & mask) != 0
                if on:
                    self.set_pixel(x + col, y + row, fg)
                elif bg is not None:
                    self.set_pixel(x + col, y + row, bg)

    def draw_asset(
        self,
        x: int,
        y: int,
        asset_id: GfxAsset,
        fg: Color,
        bg: Color | None = Color.WHITE,
    ) -> None:
        asset = get_asset(asset_id)
        self.draw_bitmap(x, y, asset.width, asset.height, asset.data, fg, bg)

    @staticmethod
    def font_width(size: FontSize) -> int:
        return int(size) // 2

    def draw_char(self, x: int, y: int, ch: str, size: FontSize, color: Color) -> None:
        if len(ch) != 1 or ch < " " or ch > "~":
            return
        glyphs = _FONTS[size]
        glyph = glyphs[ord(ch) - 0x20]
        px, py = x, y
        y0 = y
        for byte in glyph:
            temp = byte
            for _ in range(8):
                if temp & 0x80:
                    self.set_pixel(px, py, color)
                temp = (temp << 1) & 0xFF
                py += 1
                if (py - y0) == int(size):
                    py = y0
                    px += 1
                    break

    def draw_string(self, x: int, y: int, text: str, size: FontSize, color: Color) -> None:
        cx, cy = x, y
        step = self.font_width(size)
        for ch in text:
            if ch < " " or ch > "~":
                continue
            self.draw_char(cx, cy, ch, size, color)
            cx += step
            if cx > WIDTH - step:
                cx = 0
                cy += int(size)
                if cy >= HEIGHT:
                    break

    def planes(self) -> tuple[bytes, bytes]:
        return bytes(self.bw), bytes(self.red)

    def preview_rgb(self, scale: int = 2) -> bytes:
        """
        Pack RGB888 top-left logical pixels for Tk PhotoImage / preview.
        Returns raw bytes length = (WIDTH*scale)*(HEIGHT*scale)*3.
        """
        if scale < 1:
            raise ValueError("scale must be >= 1")
        out = bytearray(WIDTH * scale * HEIGHT * scale * 3)
        for ly in range(HEIGHT):
            ry = (HEIGHT - 1 - ly) if FLIP_Y else ly
            row_off = ry * ROW_BYTES
            for lx in range(WIDTH):
                idx = row_off + (lx // 8)
                bit = 7 - (lx % 8)
                mask = 1 << bit
                if self.red[idx] & mask:
                    r, g, b = 200, 24, 24
                elif self.bw[idx] & mask:
                    r, g, b = 245, 245, 240
                else:
                    r, g, b = 16, 16, 16
                for sy in range(scale):
                    for sx in range(scale):
                        ox = lx * scale + sx
                        oy = ly * scale + sy
                        p = (oy * WIDTH * scale + ox) * 3
                        out[p] = r
                        out[p + 1] = g
                        out[p + 2] = b
        return bytes(out)
