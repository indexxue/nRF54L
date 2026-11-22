"""CMD IDs aligned with ble/include/ble_cmd.h."""

from __future__ import annotations

import struct
from typing import Any

from .codec import CmdFrame

BLE_CMD_PING = 1001
BLE_CMD_GET_SLOT_STATUS = 1002
BLE_CMD_DISPLAY = 2001
BLE_CMD_DISP_BEGIN = 2002
BLE_CMD_DISP_CHUNK = 2003
BLE_CMD_DISP_REFRESH = 2004
BLE_CMD_SET_GROUP_META = 4001
BLE_CMD_SET_HOME_OVERLAY = 4002  # flags: bit0=页数 bit1=电量占位
BLE_CMD_DELETE_LAST_PAGE = 4003  # payload: mode u8
BLE_CMD_ACK = 7001
BLE_CMD_HEARTBEAT = 8001
BLE_CMD_SLOT_STATUS = 8002

BLE_CMD_DISP_PLANE_BW = 0
BLE_CMD_DISP_PLANE_RED = 1
BLE_CMD_DISP_PLANES_BW = 1 << 0
BLE_CMD_DISP_PLANES_RED = 1 << 1

GROUP_TITLE_MAX = 31
GROUP_TITLE_BUF = 32
SET_GROUP_META_PAYLOAD_SIZE = 35  # mode + count + title_len + title[32]
SLOT_STATUS_PAYLOAD_SIZE = 73

MODE_HOME = 0
MODE_NOTES = 1
MODE_INFO = 2

NOTES_CONTENT_MAX = 7
INFO_CONTENT_MAX = 3

_CMD_NAMES = {
    BLE_CMD_PING: "PING",
    BLE_CMD_GET_SLOT_STATUS: "GET_SLOT_STATUS",
    BLE_CMD_DISPLAY: "DISPLAY",
    BLE_CMD_DISP_BEGIN: "DISP_BEGIN",
    BLE_CMD_DISP_CHUNK: "DISP_CHUNK",
    BLE_CMD_DISP_REFRESH: "DISP_REFRESH",
    BLE_CMD_SET_GROUP_META: "SET_GROUP_META",
    BLE_CMD_SET_HOME_OVERLAY: "SET_HOME_OVERLAY",
    BLE_CMD_DELETE_LAST_PAGE: "DELETE_LAST_PAGE",
    BLE_CMD_ACK: "ACK",
    BLE_CMD_HEARTBEAT: "HEARTBEAT",
    BLE_CMD_SLOT_STATUS: "SLOT_STATUS",
}


def cmd_name(cmd_id: int) -> str:
    return _CMD_NAMES.get(cmd_id, f"CMD_{cmd_id}")


def utf8_fit(text: str, max_bytes: int = GROUP_TITLE_MAX) -> bytes:
    """Encode UTF-8 and truncate on character boundary to ≤ max_bytes."""
    out = b""
    for ch in text:
        piece = ch.encode("utf-8")
        if len(out) + len(piece) > max_bytes:
            break
        out += piece
    return out


def pack_set_group_meta(mode: int, content_count: int, title: str) -> bytes:
    """4001 payload: mode + count + title_len + title[32]."""
    if mode not in (MODE_NOTES, MODE_INFO):
        raise ValueError(f"SET_GROUP_META mode must be notes/info, got {mode}")
    max_c = NOTES_CONTENT_MAX if mode == MODE_NOTES else INFO_CONTENT_MAX
    if content_count < 0 or content_count > max_c:
        raise ValueError(f"content_count out of range 0..{max_c}")
    title_b = utf8_fit(title, GROUP_TITLE_MAX)
    buf = bytearray(SET_GROUP_META_PAYLOAD_SIZE)
    buf[0] = mode & 0xFF
    buf[1] = content_count & 0xFF
    buf[2] = len(title_b)
    buf[3 : 3 + len(title_b)] = title_b
    return bytes(buf)


def parse_slot_status(payload: bytes) -> dict[str, Any]:
    if len(payload) < SLOT_STATUS_PAYLOAD_SIZE:
        raise ValueError(f"SLOT_STATUS payload too short: {len(payload)}")

    def _title(off_len: int, off_buf: int) -> tuple[int, str]:
        tlen = payload[off_len]
        if tlen > GROUP_TITLE_MAX:
            tlen = GROUP_TITLE_MAX
        raw = payload[off_buf : off_buf + tlen]
        try:
            text = raw.decode("utf-8")
        except UnicodeDecodeError:
            text = raw.decode("utf-8", errors="replace")
        return tlen, text

    notes_tlen, notes_title = _title(4, 5)
    info_tlen, info_title = _title(38, 39)
    return {
        "cur_mode": payload[0],
        "cur_page": payload[1],
        "home_valid": bool(payload[2]),
        "notes_count": payload[3],
        "notes_title_len": notes_tlen,
        "notes_title": notes_title,
        "info_count": payload[37],
        "info_title_len": info_tlen,
        "info_title": info_title,
        "notes_valid_bits": payload[71],
        "info_valid_bits": payload[72],
    }


def parse_ack_payload(payload: bytes) -> dict[str, Any]:
    if len(payload) < 6:
        raise ValueError(f"ACK payload too short: {len(payload)}")
    req_cmd_id, status = struct.unpack_from("<Hi", payload, 0)
    return {"req_cmd_id": req_cmd_id, "status": status}


def parse_heartbeat_payload(payload: bytes) -> dict[str, Any]:
    if len(payload) < 5:
        raise ValueError(f"HEARTBEAT payload too short: {len(payload)}")
    tick, flags = struct.unpack_from("<IB", payload, 0)
    return {
        "tick": tick,
        "flags": flags,
        "adv": bool(flags & 0x01),
        "connected": bool(flags & 0x02),
    }


def describe_frame(frame: CmdFrame) -> str:
    name = cmd_name(frame.cmd_id)
    if frame.cmd_id == BLE_CMD_ACK:
        try:
            ack = parse_ack_payload(frame.payload)
            req_name = cmd_name(ack["req_cmd_id"])
            return f"{name} req={req_name}({ack['req_cmd_id']}) status={ack['status']}"
        except ValueError:
            pass
    if frame.cmd_id == BLE_CMD_HEARTBEAT:
        try:
            hb = parse_heartbeat_payload(frame.payload)
            return (
                f"{name} tick={hb['tick']} "
                f"adv={int(hb['adv'])} connected={int(hb['connected'])}"
            )
        except ValueError:
            pass
    if frame.cmd_id == BLE_CMD_SLOT_STATUS:
        try:
            ss = parse_slot_status(frame.payload)
            return (
                f"{name} mode={ss['cur_mode']} page={ss['cur_page']} "
                f"notes={ss['notes_count']}/{ss['notes_title']!r} "
                f"info={ss['info_count']}/{ss['info_title']!r}"
            )
        except ValueError:
            pass
    if frame.cmd_id == BLE_CMD_DISP_CHUNK and frame.payload_len >= 5:
        plane, offset, data_len = struct.unpack_from("<BHH", frame.payload, 0)
        return f"{name}({frame.cmd_id}) plane={plane} off={offset} n={data_len}"
    if frame.payload_len == 0:
        return f"{name}({frame.cmd_id}) empty"
    return f"{name}({frame.cmd_id}) plen={frame.payload_len}"
