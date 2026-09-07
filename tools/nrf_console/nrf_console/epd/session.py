"""Pack DISP_BEGIN / DISP_CHUNK / DISP_REFRESH per ble/docs/display_plane_upload.md."""

from __future__ import annotations

import struct
import zlib
from collections.abc import Iterator

from nrf_console.epd.plane import HEIGHT, PLANE_SIZE, WIDTH
from nrf_console.protocol.cmds import (
    BLE_CMD_DISP_BEGIN,
    BLE_CMD_DISP_CHUNK,
    BLE_CMD_DISP_REFRESH,
    BLE_CMD_DISP_PLANE_BW,
    BLE_CMD_DISP_PLANE_RED,
    BLE_CMD_DISP_PLANES_BW,
    BLE_CMD_DISP_PLANES_RED,
)
from nrf_console.protocol.codec import PAYLOAD_MAX

CHUNK_HDR_SIZE = 5
CHUNK_DATA_MAX = PAYLOAD_MAX - CHUNK_HDR_SIZE  # 235


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def pack_begin(bw: bytes | None, red: bytes | None) -> bytes:
    if bw is None and red is None:
        raise ValueError("need at least one plane")
    if bw is not None and len(bw) != PLANE_SIZE:
        raise ValueError(f"bw len {len(bw)} != {PLANE_SIZE}")
    if red is not None and len(red) != PLANE_SIZE:
        raise ValueError(f"red len {len(red)} != {PLANE_SIZE}")

    planes = 0
    bw_len = 0
    red_len = 0
    bw_crc = 0
    red_crc = 0
    if bw is not None:
        planes |= BLE_CMD_DISP_PLANES_BW
        bw_len = PLANE_SIZE
        bw_crc = crc32(bw)
    if red is not None:
        planes |= BLE_CMD_DISP_PLANES_RED
        red_len = PLANE_SIZE
        red_crc = crc32(red)

    return struct.pack(
        "<HHBBHHII",
        WIDTH,
        HEIGHT,
        planes,
        0,
        bw_len,
        red_len,
        bw_crc,
        red_crc,
    )


def pack_chunk(plane: int, offset: int, data: bytes) -> bytes:
    if plane not in (BLE_CMD_DISP_PLANE_BW, BLE_CMD_DISP_PLANE_RED):
        raise ValueError(f"bad plane id: {plane}")
    if not data:
        raise ValueError("empty chunk")
    if len(data) > CHUNK_DATA_MAX:
        raise ValueError(f"chunk data {len(data)} > {CHUNK_DATA_MAX}")
    if offset < 0 or offset + len(data) > PLANE_SIZE:
        raise ValueError("chunk out of plane range")
    return struct.pack("<BHH", plane, offset, len(data)) + data


def iter_chunks(plane: int, data: bytes, max_n: int = CHUNK_DATA_MAX) -> Iterator[bytes]:
    if len(data) != PLANE_SIZE:
        raise ValueError(f"plane len {len(data)} != {PLANE_SIZE}")
    off = 0
    while off < len(data):
        n = min(max_n, len(data) - off)
        yield pack_chunk(plane, off, data[off : off + n])
        off += n


def iter_upload_steps(
    bw: bytes | None,
    red: bytes | None,
    *,
    chunk_data_max: int = CHUNK_DATA_MAX,
) -> Iterator[tuple[int, bytes]]:
    """Yield (cmd_id, payload) for a full upload session."""
    if chunk_data_max < 1:
        raise ValueError("chunk_data_max must be >= 1")
    if chunk_data_max > CHUNK_DATA_MAX:
        chunk_data_max = CHUNK_DATA_MAX

    yield BLE_CMD_DISP_BEGIN, pack_begin(bw, red)
    if bw is not None:
        for payload in iter_chunks(BLE_CMD_DISP_PLANE_BW, bw, max_n=chunk_data_max):
            yield BLE_CMD_DISP_CHUNK, payload
    if red is not None:
        for payload in iter_chunks(BLE_CMD_DISP_PLANE_RED, red, max_n=chunk_data_max):
            yield BLE_CMD_DISP_CHUNK, payload
    yield BLE_CMD_DISP_REFRESH, b""
