"""DISP_BEGIN pack for ink_panel — β=20B(+mode/page)。"""

from __future__ import annotations

from collections.abc import Iterator

from .paths import ensure_nrf_console_on_path

ensure_nrf_console_on_path()

from nrf_console.epd.plane import PANEL_4IN2  # noqa: E402
from nrf_console.epd.session import (  # noqa: E402
    CHUNK_DATA_MAX,
    FLAG_SHOW,
    crc32,
    iter_chunks,
    pack_begin as _pack_begin_base,
)
from nrf_console.protocol.cmds import (  # noqa: E402
    BLE_CMD_DISP_BEGIN,
    BLE_CMD_DISP_CHUNK,
    BLE_CMD_DISP_PLANE_BW,
    BLE_CMD_DISP_PLANE_RED,
    BLE_CMD_DISP_REFRESH,
)

PANEL = PANEL_4IN2
__all__ = [
    "FLAG_SHOW",
    "PANEL",
    "crc32",
    "iter_chunks",
    "iter_upload_steps",
    "pack_begin",
]


def pack_begin(
    bw: bytes | None,
    red: bytes | None,
    *,
    mode: int = 0,
    page_id: int = 0,
    show: bool = True,
    with_slot: bool = True,
) -> bytes:
    """ink_panel β 默认 20B；with_slot=False 仅调试用（固件会拒 18B）。"""
    if with_slot:
        return _pack_begin_base(
            bw,
            red,
            width=PANEL.width,
            height=PANEL.height,
            mode=mode,
            page_id=page_id,
            show=show,
        )
    return _pack_begin_base(bw, red, width=PANEL.width, height=PANEL.height)


def iter_upload_steps(
    bw: bytes | None,
    red: bytes | None,
    *,
    mode: int = 0,
    page_id: int = 0,
    show: bool = True,
    with_slot: bool = True,
    chunk_data_max: int = CHUNK_DATA_MAX,
) -> Iterator[tuple[int, bytes]]:
    if chunk_data_max < 1:
        raise ValueError("chunk_data_max must be >= 1")
    chunk_data_max = min(chunk_data_max, CHUNK_DATA_MAX)
    plane_size = PANEL.plane_size

    yield BLE_CMD_DISP_BEGIN, pack_begin(
        bw, red, mode=mode, page_id=page_id, show=show, with_slot=with_slot
    )
    if bw is not None:
        for payload in iter_chunks(
            BLE_CMD_DISP_PLANE_BW, bw, plane_size=plane_size, max_n=chunk_data_max
        ):
            yield BLE_CMD_DISP_CHUNK, payload
    if red is not None:
        for payload in iter_chunks(
            BLE_CMD_DISP_PLANE_RED, red, plane_size=plane_size, max_n=chunk_data_max
        ):
            yield BLE_CMD_DISP_CHUNK, payload
    yield BLE_CMD_DISP_REFRESH, b""
