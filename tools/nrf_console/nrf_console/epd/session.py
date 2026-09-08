"""Pack DISP_BEGIN / DISP_CHUNK / DISP_REFRESH per ble/docs/display_plane_upload.md."""

from __future__ import annotations

import struct
import zlib
from collections.abc import Iterator

from nrf_console.epd.plane import (
    PanelProfile,
    geometry_for,
    panel_by_plane_size,
)
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


def resolve_panel(
    bw: bytes | None,
    red: bytes | None,
    *,
    width: int | None = None,
    height: int | None = None,
) -> PanelProfile:
    """
    Resolve geometry for upload.
    Prefer explicit width/height (any size). Else infer from known plane lengths.
    Device size check is firmware DISP_BEGIN — host does not whitelist panels.
    """
    if width is not None and height is not None:
        return geometry_for(width, height)

    sample = bw if bw is not None else red
    if sample is None:
        raise ValueError("need at least one plane")
    panel = panel_by_plane_size(len(sample))
    if panel is None:
        raise ValueError(
            f"cannot infer size from plane len={len(sample)}; pass width/height"
        )
    return panel


def pack_begin(
    bw: bytes | None,
    red: bytes | None,
    *,
    width: int | None = None,
    height: int | None = None,
    panel: PanelProfile | None = None,
) -> bytes:
    if bw is None and red is None:
        raise ValueError("need at least one plane")
    panel = panel or resolve_panel(bw, red, width=width, height=height)
    plane_size = panel.plane_size

    if bw is not None and len(bw) != plane_size:
        raise ValueError(f"bw len {len(bw)} != {plane_size} ({panel.width}x{panel.height})")
    if red is not None and len(red) != plane_size:
        raise ValueError(f"red len {len(red)} != {plane_size} ({panel.width}x{panel.height})")

    planes = 0
    bw_len = 0
    red_len = 0
    bw_crc = 0
    red_crc = 0
    if bw is not None:
        planes |= BLE_CMD_DISP_PLANES_BW
        bw_len = plane_size
        bw_crc = crc32(bw)
    if red is not None:
        planes |= BLE_CMD_DISP_PLANES_RED
        red_len = plane_size
        red_crc = crc32(red)

    return struct.pack(
        "<HHBBHHII",
        panel.width,
        panel.height,
        planes,
        0,
        bw_len,
        red_len,
        bw_crc,
        red_crc,
    )


def pack_chunk(plane: int, offset: int, data: bytes, *, plane_size: int) -> bytes:
    if plane not in (BLE_CMD_DISP_PLANE_BW, BLE_CMD_DISP_PLANE_RED):
        raise ValueError(f"bad plane id: {plane}")
    if not data:
        raise ValueError("empty chunk")
    if len(data) > CHUNK_DATA_MAX:
        raise ValueError(f"chunk data {len(data)} > {CHUNK_DATA_MAX}")
    if offset < 0 or offset + len(data) > plane_size:
        raise ValueError("chunk out of plane range")
    return struct.pack("<BHH", plane, offset, len(data)) + data


def iter_chunks(
    plane: int,
    data: bytes,
    *,
    plane_size: int,
    max_n: int = CHUNK_DATA_MAX,
) -> Iterator[bytes]:
    if len(data) != plane_size:
        raise ValueError(f"plane len {len(data)} != {plane_size}")
    off = 0
    while off < len(data):
        n = min(max_n, len(data) - off)
        yield pack_chunk(plane, off, data[off : off + n], plane_size=plane_size)
        off += n


def iter_upload_steps(
    bw: bytes | None,
    red: bytes | None,
    *,
    width: int | None = None,
    height: int | None = None,
    chunk_data_max: int = CHUNK_DATA_MAX,
) -> Iterator[tuple[int, bytes]]:
    """Yield (cmd_id, payload) for a full upload session."""
    if chunk_data_max < 1:
        raise ValueError("chunk_data_max must be >= 1")
    if chunk_data_max > CHUNK_DATA_MAX:
        chunk_data_max = CHUNK_DATA_MAX

    panel = resolve_panel(bw, red, width=width, height=height)
    plane_size = panel.plane_size

    yield BLE_CMD_DISP_BEGIN, pack_begin(bw, red, panel=panel)
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
