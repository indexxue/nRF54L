"""Composable full-page layouts for BLE plane upload."""

from __future__ import annotations

from collections.abc import Callable
from typing import Any

from nrf_console.epd.assets import GfxAsset
from nrf_console.epd.display import Canvas, FontSize
from nrf_console.epd.plane import HEIGHT, WIDTH, Color, make_smoke_bwr


def make_gfx_demo() -> tuple[bytes, bytes]:
    """Mirror cbb/epd epd_display_gfx_demo_fill()."""
    c = Canvas()
    c.clear(Color.WHITE)
    font = FontSize.FONT_12

    c.fill_rect(0, 0, WIDTH - 1, 3, Color.BLACK)
    c.draw_string(4, 8, "GFX/ICON TEST", font, Color.BLACK)

    c.draw_string(4, 24, "SHAPES:", font, Color.BLACK)
    x0, y0 = 4, 24
    c.fill_rect(x0, y0 + 14, x0 + 31, y0 + 45, Color.BLACK)
    c.draw_rect(x0 + 40, y0 + 14, x0 + 75, y0 + 45, Color.BLACK)
    c.draw_line(x0, y0 + 52, x0 + 75, y0 + 52, Color.BLACK)
    c.draw_line(x0 + 40, y0 + 52, x0 + 75, y0 + 79, Color.BLACK)
    c.draw_line(x0, y0 + 79, x0 + 75, y0 + 52, Color.BLACK)

    c.draw_string(4, 110, "ICONS:", font, Color.BLACK)
    c.draw_asset(4, 124, GfxAsset.ICON_HEART_16, Color.BLACK, Color.WHITE)
    c.draw_asset(28, 124, GfxAsset.ICON_CHECK_16, Color.BLACK, Color.WHITE)
    c.draw_asset(52, 124, GfxAsset.ICON_ARROW_16, Color.BLACK, Color.WHITE)

    c.draw_string(4, 146, "BITMAP:", font, Color.BLACK)
    c.draw_asset(4, 160, GfxAsset.BITMAP_CHECKER_32, Color.BLACK, Color.WHITE)

    c.draw_string(4, 200, "RED ICON:", font, Color.BLACK)
    c.draw_asset(4, 214, GfxAsset.ICON_HEART_16, Color.RED, Color.WHITE)

    c.draw_string(4, 238, "GFX TEST OK", font, Color.BLACK)
    return c.planes()


def make_test_pattern() -> tuple[bytes, bytes]:
    """Mirror cbb/epd fill_test_pattern()."""
    c = Canvas()
    c.clear(Color.WHITE)
    mid_x = WIDTH // 2
    if HEIGHT < 50 or mid_x == 0:
        return c.planes()

    c.fill_rect(0, 0, mid_x - 1, 49, Color.RED)
    for x in range(0, WIDTH, 16):
        c.fill_rect(x, 50, min(x + 7, WIDTH - 1), 99, Color.RED)

    if HEIGHT > 149:
        c.fill_rect(0, 100, WIDTH - 1, 149, Color.BLACK)
        if WIDTH > 16:
            c.fill_rect(8, 118, WIDTH - 9, 131, Color.RED)

    if HEIGHT > 199:
        c.fill_rect(0, 150, 31, 199, Color.RED)
        for y in range(150, 200):
            for x in range(40, WIDTH):
                black = ((x // 16) + (y // 16)) % 2 == 0
                c.set_pixel(x, y, Color.BLACK if black else Color.WHITE)

    if HEIGHT > 200:
        c.draw_rect(0, 200, WIDTH - 1, HEIGHT - 1, Color.RED)

    return c.planes()


def build_tag(
    title: str = "eink_tag",
    price: str = "12.80",
    sku: str = "SKU-029Z95",
) -> tuple[bytes, bytes]:
    """ESL-style price tag (ASCII only; matches epd_display font)."""
    c = Canvas()
    c.clear(Color.WHITE)

    title_s = _ascii(title, 18) or "eink_tag"
    price_s = _ascii(price, 12) or "0.00"
    sku_s = _ascii(sku, 20) or "SKU"

    c.fill_rect(0, 0, WIDTH - 1, 28, Color.BLACK)
    c.draw_string(6, 8, title_s, FontSize.FONT_16, Color.WHITE)

    c.draw_string(6, 44, "PRICE", FontSize.FONT_12, Color.BLACK)
    c.draw_string(6, 62, f"CNY {price_s}"[:18], FontSize.FONT_24, Color.RED)

    c.draw_line(6, 100, WIDTH - 7, 100, Color.BLACK)
    c.draw_string(6, 112, sku_s, FontSize.FONT_12, Color.BLACK)
    c.draw_string(6, 132, "GDEY029Z95 128x296", FontSize.FONT_12, Color.BLACK)

    c.draw_asset(6, 160, GfxAsset.ICON_CHECK_16, Color.BLACK, Color.WHITE)
    c.draw_string(28, 164, "BLE upload OK", FontSize.FONT_12, Color.BLACK)

    c.draw_rect(0, 0, WIDTH - 1, HEIGHT - 1, Color.BLACK)
    c.fill_rect(0, HEIGHT - 24, WIDTH - 1, HEIGHT - 1, Color.RED)
    c.draw_string(10, HEIGHT - 18, "nRF54L15", FontSize.FONT_12, Color.WHITE)
    return c.planes()


def make_tag_demo(
    title: str = "eink_tag",
    price: str = "12.80",
    sku: str = "SKU-029Z95",
) -> tuple[bytes, bytes]:
    """Alias for build_tag (backward compatible)."""
    return build_tag(title=title, price=price, sku=sku)


def _ascii(text: str, max_len: int) -> str:
    out = "".join(ch if " " <= ch <= "~" else "?" for ch in text)
    return out[:max_len]


# Layouts that accept no fields (fixed art).
FIXED_LAYOUTS = frozenset({"smoke", "gfx_demo", "test_pattern"})
# Layouts with editable title/price/sku.
FIELD_LAYOUTS = frozenset({"tag"})

PageBuilder = Callable[..., tuple[bytes, bytes]]

PAGE_CATALOG: dict[str, tuple[str, PageBuilder]] = {
    "tag": ("Price tag", build_tag),
    "smoke": ("Smoke bars (B/R/W)", make_smoke_bwr),
    "gfx_demo": ("GFX / icon demo", make_gfx_demo),
    "test_pattern": ("Factory test pattern", make_test_pattern),
}


def build_page(key: str, **kwargs: Any) -> tuple[bytes, bytes]:
    if key == "tag_demo":
        key = "tag"
    if key not in PAGE_CATALOG:
        raise KeyError(f"unknown page: {key}")
    title, builder = PAGE_CATALOG[key]
    _ = title
    if key in FIELD_LAYOUTS:
        return builder(
            title=str(kwargs.get("title", "eink_tag")),
            price=str(kwargs.get("price", "12.80")),
            sku=str(kwargs.get("sku", "SKU-029Z95")),
        )
    return builder()


def page_preview_rgb(key: str, scale: int = 2, **kwargs: Any) -> bytes:
    bw, red = build_page(key, **kwargs)
    c = Canvas()
    c.bw[:] = bw
    c.red[:] = red
    return c.preview_rgb(scale=scale)


__all__ = [
    "FIELD_LAYOUTS",
    "FIXED_LAYOUTS",
    "PAGE_CATALOG",
    "build_page",
    "build_tag",
    "make_gfx_demo",
    "make_smoke_bwr",
    "make_tag_demo",
    "make_test_pattern",
    "page_preview_rgb",
]
