"""DISP_BEGIN pack for ink_panel — α=18B；β=20B(+mode/page)。"""

from __future__ import annotations

import struct
from collections.abc import Iterator

from .paths import ensure_nrf_console_on_path

ensure_nrf_console_on_path()

from nrf_console.epd.plane import PANEL_4IN2  # noqa: E402
from nrf_console.epd.session import (  # noqa: E402
    CHUNK_DATA_MAX,
    crc32,
    iter_chunks,
)
from nrf_console.protocol.cmds import (  # noqa: E402
    BLE_CMD_DISP_BEGIN,
    BLE_CMD_DISP_CHUNK,
    BLE_CMD_DISP_PLANE_BW,
    BLE_CMD_DISP_PLANE_RED,
    BLE_CMD_DISP_PLANES_BW,
    BLE_CMD_DISP_PLANES_RED,
    BLE_CMD_DISP_REFRESH,
)

PANEL = PANEL_4IN2
FLAG_SHOW = 1 << 0


def pack_begin(
    bw: bytes | None,
    red: bytes | None,
    *,
    mode: int = 0,
    page_id: int = 0,
    show: bool = True,
    with_slot: bool = False,
) -> bytes:
    if bw is None and red is None:
        raise ValueError("need at least one plane")
    plane_size = PANEL.plane_size
    if bw is not None and len(bw) != plane_size:
        raise ValueError(f"bw len {len(bw)} != {plane_size}")
    if red is not None and len(red) != plane_size:
        raise ValueError(f"red len {len(red)} != {plane_size}")

    planes = 0
    bw_len = red_len = 0
    bw_crc = red_crc = 0
    if bw is not None:
        planes |= BLE_CMD_DISP_PLANES_BW
        bw_len = plane_size
        bw_crc = crc32(bw)
    if red is not None:
        planes |= BLE_CMD_DISP_PLANES_RED
        red_len = plane_size
        red_crc = crc32(red)

    flags = FLAG_SHOW if (with_slot and show) else 0
    base = struct.pack(
        "<HHBBHHII",
        PANEL.width,
        PANEL.height,
        planes,
        flags,
        bw_len,
        red_len,
        bw_crc,
        red_crc,
    )
    if not with_slot:
        # α：flags 必须为 0，固件仍校验 flags==0
        return struct.pack(
            "<HHBBHHII",
            PANEL.width,
            PANEL.height,
            planes,
            0,
            bw_len,
            red_len,
            bw_crc,
            red_crc,
        )
    return base + struct.pack("<BB", mode & 0xFF, page_id & 0xFF)


def iter_upload_steps(
    bw: bytes | None,
    red: bytes | None,
    *,
    mode: int = 0,
    page_id: int = 0,
    show: bool = True,
    with_slot: bool = False,
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
