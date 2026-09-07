"""CMD IDs aligned with ble/include/ble_cmd.h."""

from __future__ import annotations

import struct
from typing import Any

from .codec import CmdFrame

BLE_CMD_PING = 1001
BLE_CMD_DISPLAY = 2001
BLE_CMD_DISP_BEGIN = 2002
BLE_CMD_DISP_CHUNK = 2003
BLE_CMD_DISP_REFRESH = 2004
BLE_CMD_ACK = 7001
BLE_CMD_HEARTBEAT = 8001

BLE_CMD_DISP_PLANE_BW = 0
BLE_CMD_DISP_PLANE_RED = 1
BLE_CMD_DISP_PLANES_BW = 1 << 0
BLE_CMD_DISP_PLANES_RED = 1 << 1

_CMD_NAMES = {
    BLE_CMD_PING: "PING",
    BLE_CMD_DISPLAY: "DISPLAY",
    BLE_CMD_DISP_BEGIN: "DISP_BEGIN",
    BLE_CMD_DISP_CHUNK: "DISP_CHUNK",
    BLE_CMD_DISP_REFRESH: "DISP_REFRESH",
    BLE_CMD_ACK: "ACK",
    BLE_CMD_HEARTBEAT: "HEARTBEAT",
}


def cmd_name(cmd_id: int) -> str:
    return _CMD_NAMES.get(cmd_id, f"CMD_{cmd_id}")


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
    if frame.cmd_id == BLE_CMD_DISP_CHUNK and frame.payload_len >= 5:
        plane, offset, data_len = struct.unpack_from("<BHH", frame.payload, 0)
        return f"{name}({frame.cmd_id}) plane={plane} off={offset} n={data_len}"
    if frame.payload_len == 0:
        return f"{name}({frame.cmd_id}) empty"
    return f"{name}({frame.cmd_id}) plen={frame.payload_len}"
