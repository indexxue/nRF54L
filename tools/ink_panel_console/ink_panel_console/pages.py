"""Built-in 4.2" page templates (bands / home / notes / info)."""

from __future__ import annotations

from collections.abc import Callable

from .panel import PANEL, Color, fill_rect, make_empty


def _border(bw: bytearray, red: bytearray) -> None:
    w, h = PANEL.width, PANEL.height
    fill_rect(bw, red, 0, 0, w - 1, 0, Color.BLACK)
    fill_rect(bw, red, 0, h - 1, w - 1, h - 1, Color.BLACK)
    fill_rect(bw, red, 0, 0, 0, h - 1, Color.BLACK)
    fill_rect(bw, red, w - 1, 0, w - 1, h - 1, Color.BLACK)


def make_bands() -> tuple[bytes, bytes]:
    """三竖条黑/红/白 — 目视确认 4.2" BWR。"""
    bw, red = make_empty()
    w, h = PANEL.width, PANEL.height
    col = w // 3
    fill_rect(bw, red, 0, 0, col - 1, h - 1, Color.BLACK)
    fill_rect(bw, red, col, 0, col * 2 - 1, h - 1, Color.RED)
    fill_rect(bw, red, col * 2, 0, w - 1, h - 1, Color.WHITE)
    _border(bw, red)
    return bytes(bw), bytes(red)


def make_home() -> tuple[bytes, bytes]:
    """首页占位：顶黑条 + 中红块（Logo 请用 --image 推 logo_bwr）。"""
    bw, red = make_empty()
    w, h = PANEL.width, PANEL.height
    fill_rect(bw, red, 0, 0, w - 1, 40, Color.BLACK)
    fill_rect(bw, red, 80, 100, w - 81, 220, Color.RED)
    _border(bw, red)
    return bytes(bw), bytes(red)


def make_notes() -> tuple[bytes, bytes]:
    """记事占位：三行勾选条。"""
    bw, red = make_empty()
    w, h = PANEL.width, PANEL.height
    fill_rect(bw, red, 0, 0, w - 1, 36, Color.BLACK)
    for i, y in enumerate((60, 120, 180)):
        fill_rect(bw, red, 24, y, 56, y + 32, Color.BLACK)
        if i == 0:
            fill_rect(bw, red, 28, y + 4, 52, y + 28, Color.RED)
        fill_rect(bw, red, 72, y + 8, w - 40, y + 24, Color.BLACK)
    _border(bw, red)
    return bytes(bw), bytes(red)


def make_info() -> tuple[bytes, bytes]:
    """信息牌占位：大红状态区。"""
    bw, red = make_empty()
    w, h = PANEL.width, PANEL.height
    fill_rect(bw, red, 0, 0, w - 1, 48, Color.BLACK)
    fill_rect(bw, red, 40, 80, w - 41, 220, Color.RED)
    fill_rect(bw, red, 0, h - 24, w - 1, h - 1, Color.BLACK)
    _border(bw, red)
    return bytes(bw), bytes(red)


PAGE_BUILDERS: dict[str, Callable[[], tuple[bytes, bytes]]] = {
    "bands": make_bands,
    "home": make_home,
    "notes": make_notes,
    "info": make_info,
}

# 默认槽：模板名 → (mode, page_id)；notes/info 内容页从 1 起（page0 非内容）
PAGE_DEFAULT_SLOT: dict[str, tuple[int, int]] = {
    "bands": (0, 0),
    "home": (0, 0),
    "notes": (1, 1),
    "info": (2, 1),
}
