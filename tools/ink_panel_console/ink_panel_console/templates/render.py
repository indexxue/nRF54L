"""今日三件事 / 门牌状态 → 400×300 BWR。"""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from pathlib import Path

from ink_panel_console.panel import PANEL, Color, fill_rect, make_empty, set_pixel

_FONT_CACHE: dict[tuple[str, int], object] = {}


def resolve_cjk_font() -> Path | None:
    """Windows / 常见 CJK 字体回退链。"""
    windir = os.environ.get("WINDIR", r"C:\Windows")
    candidates = [
        Path(windir) / "Fonts" / "msyh.ttc",
        Path(windir) / "Fonts" / "msyh.ttf",
        Path(windir) / "Fonts" / "simhei.ttf",
        Path(windir) / "Fonts" / "simsun.ttc",
        Path("/System/Library/Fonts/PingFang.ttc"),
        Path("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"),
    ]
    for p in candidates:
        if p.is_file():
            return p
    return None


def _load_font(size: int):
    try:
        from PIL import ImageFont
    except ImportError as exc:  # pragma: no cover
        raise RuntimeError("Pillow required — pip install Pillow") from exc

    key = ("cjk", size)
    if key in _FONT_CACHE:
        return _FONT_CACHE[key]
    path = resolve_cjk_font()
    if path is None:
        font = ImageFont.load_default()
    else:
        try:
            font = ImageFont.truetype(str(path), size=size)
        except OSError:
            font = ImageFont.load_default()
    _FONT_CACHE[key] = font
    return font


def _rgb_to_planes(img) -> tuple[bytes, bytes]:
    """RGB → BWR（红优先，再黑白）。"""
    bw, red = make_empty()
    pixels = img.convert("RGB").load()
    assert pixels is not None
    w, h = PANEL.width, PANEL.height
    for y in range(h):
        for x in range(w):
            r, g, b = pixels[x, y]
            lum = (r * 3 + g * 6 + b) // 10
            sat = max(r, g, b) - min(r, g, b)
            if r >= g + 30 and r >= b + 30 and sat >= 40:
                set_pixel(bw, red, x, y, Color.RED)
            elif lum < 140:
                set_pixel(bw, red, x, y, Color.BLACK)
            else:
                set_pixel(bw, red, x, y, Color.WHITE)
    return bytes(bw), bytes(red)


def _draw_border(draw, w: int, h: int) -> None:
    draw.rectangle([0, 0, w - 1, h - 1], outline=(0, 0, 0), width=2)


@dataclass
class NotesForm:
    title: str = "今日三件事"
    items: list[str] = field(default_factory=lambda: ["", "", ""])
    accent: int = 0  # 1..3 红强调；0=无
    page: int = 1  # 内容页号；只显示「第 N 页」，不印总页


@dataclass
class InfoForm:
    title: str = "信息牌"
    status: str = "空闲"
    status_red: bool = True
    note: str = ""
    page: int = 1


def _page_badge(draw, font, page: int, w: int) -> None:
    """右上角页码徽章（仅当前页，无总页）。"""
    if page < 1:
        return
    text = f"第 {page} 页"
    try:
        bbox = draw.textbbox((0, 0), text, font=font)
        tw = bbox[2] - bbox[0]
    except Exception:  # noqa: BLE001
        tw = len(text) * 10
    x = max(8, w - tw - 20)
    draw.rectangle([x - 10, 6, w - 6, 42], fill=(194, 59, 42))
    draw.text((x, 10), text, font=font, fill=(255, 255, 255))


def render_notes_three(form: NotesForm) -> tuple[bytes, bytes]:
    from PIL import Image, ImageDraw

    w, h = PANEL.width, PANEL.height
    img = Image.new("RGB", (w, h), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    title_font = _load_font(28)
    body_font = _load_font(22)
    small_font = _load_font(16)
    badge_font = _load_font(18)

    # 顶栏
    draw.rectangle([0, 0, w, 48], fill=(0, 0, 0))
    draw.text((16, 10), (form.title or "今日三件事")[:14], font=title_font, fill=(255, 255, 255))
    _page_badge(draw, badge_font, form.page, w)

    items = list(form.items) + ["", "", ""]
    items = items[:3]
    y0 = 70
    for i, text in enumerate(items):
        y = y0 + i * 70
        box = [20, y, w - 20, y + 56]
        accent = form.accent == (i + 1)
        if accent:
            draw.rectangle(box, outline=(194, 59, 42), width=3)
            draw.ellipse([28, y + 14, 56, y + 42], outline=(194, 59, 42), width=2)
        else:
            draw.rectangle(box, outline=(0, 0, 0), width=2)
            draw.ellipse([28, y + 14, 56, y + 42], outline=(0, 0, 0), width=2)
        label = text.strip() or f"事项 {i + 1}"
        color = (194, 59, 42) if accent else (0, 0, 0)
        draw.text((70, y + 14), label[:18], font=body_font, fill=color)

    draw.text((16, h - 28), "ink_panel · 记事", font=small_font, fill=(80, 80, 80))
    _draw_border(draw, w, h)
    return _rgb_to_planes(img)


def render_info_door(form: InfoForm) -> tuple[bytes, bytes]:
    from PIL import Image, ImageDraw

    w, h = PANEL.width, PANEL.height
    img = Image.new("RGB", (w, h), (255, 255, 255))
    draw = ImageDraw.Draw(img)
    title_font = _load_font(30)
    status_font = _load_font(42)
    note_font = _load_font(20)
    small_font = _load_font(16)
    badge_font = _load_font(18)

    draw.rectangle([0, 0, w, 56], fill=(0, 0, 0))
    draw.text((16, 12), (form.title or "信息牌")[:12], font=title_font, fill=(255, 255, 255))
    _page_badge(draw, badge_font, form.page, w)

    # 状态区
    status = (form.status or "—").strip()[:12]
    if form.status_red:
        draw.rectangle([24, 80, w - 24, 200], fill=(194, 59, 42))
        draw.text((40, 120), status, font=status_font, fill=(255, 255, 255))
    else:
        draw.rectangle([24, 80, w - 24, 200], outline=(0, 0, 0), width=3)
        draw.text((40, 120), status, font=status_font, fill=(0, 0, 0))

    note = (form.note or "").strip()
    if note:
        draw.text((28, 220), note[:22], font=note_font, fill=(0, 0, 0))

    draw.rectangle([0, h - 28, w, h], fill=(0, 0, 0))
    draw.text((16, h - 24), "ink_panel · 信息", font=small_font, fill=(255, 255, 255))
    _draw_border(draw, w, h)
    return _rgb_to_planes(img)


def render_blank_home() -> tuple[bytes, bytes]:
    """首页占位：顶黑条 + 中红块（无字亦可）。"""
    bw, red = make_empty()
    w, h = PANEL.width, PANEL.height
    fill_rect(bw, red, 0, 0, w - 1, 40, Color.BLACK)
    fill_rect(bw, red, 80, 100, w - 81, 220, Color.RED)
    fill_rect(bw, red, 0, 0, w - 1, 0, Color.BLACK)
    fill_rect(bw, red, 0, h - 1, w - 1, h - 1, Color.BLACK)
    fill_rect(bw, red, 0, 0, 0, h - 1, Color.BLACK)
    fill_rect(bw, red, w - 1, 0, w - 1, h - 1, Color.BLACK)
    return bytes(bw), bytes(red)


def compose_home_overlay(
    bw: bytes,
    red: bytes,
    *,
    show_pages: bool,
    show_batt: bool,
    notes_count: int = 0,
    info_count: int = 0,
) -> tuple[bytes, bytes]:
    """
    工具预览用：左下纵向叠层（记事 / 信息 / 电量），无黑底，小字号。
    实际上屏由 MCU 叠层；此处仅预览，勿把结果当底图写入 Flash。
    """
    from PIL import Image, ImageDraw

    if not show_pages and not show_batt:
        return bw, red

    from ink_panel_console.preview_rgb import planes_to_rgb

    rgb_b = planes_to_rgb(bw, red, scale=1)
    img = Image.frombytes("RGB", (PANEL.width, PANEL.height), rgb_b)
    draw = ImageDraw.Draw(img)
    font = _load_font(12)  # 对齐屏上 EPD_FONT_12
    h = PANEL.height
    line_h = 14
    lines: list[tuple[str, tuple[int, int, int]]] = []
    if show_pages:
        lines.append((f"记事 {notes_count} 页", (0, 0, 0)))
        lines.append((f"信息 {info_count} 页", (0, 0, 0)))
    if show_batt:
        lines.append(("电量 --", (194, 59, 42)))
    ty = h - 4 - line_h * len(lines)
    if ty < 2:
        ty = 2
    for text, color in lines:
        for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            draw.text((6 + dx, ty + dy), text, font=font, fill=(255, 255, 255))
        draw.text((6, ty), text, font=font, fill=color)
        ty += line_h
    return _rgb_to_planes(img)
