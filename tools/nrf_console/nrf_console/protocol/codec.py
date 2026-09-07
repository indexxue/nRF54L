"""Binary CMD frame codec: cmd_id(u16 LE) | payload_len(u16 LE) | payload[]."""

from __future__ import annotations

import struct
from dataclasses import dataclass

HDR_SIZE = 4
PAYLOAD_MAX = 240
FRAME_MAX = HDR_SIZE + PAYLOAD_MAX


@dataclass(frozen=True)
class CmdFrame:
    cmd_id: int
    payload: bytes

    @property
    def payload_len(self) -> int:
        return len(self.payload)


def encode_frame(cmd_id: int, payload: bytes | None = None) -> bytes:
    data = payload or b""
    if len(data) > PAYLOAD_MAX:
        raise ValueError(f"payload too large: {len(data)} > {PAYLOAD_MAX}")
    return struct.pack("<HH", cmd_id & 0xFFFF, len(data)) + data


def decode_frame(raw: bytes) -> CmdFrame:
    if len(raw) < HDR_SIZE:
        raise ValueError(f"frame too short: {len(raw)}")
    cmd_id, payload_len = struct.unpack_from("<HH", raw, 0)
    if HDR_SIZE + payload_len != len(raw):
        raise ValueError(
            f"len mismatch: hdr_pl={HDR_SIZE + payload_len} frame={len(raw)}"
        )
    if payload_len > PAYLOAD_MAX:
        raise ValueError(f"payload too large: {payload_len}")
    return CmdFrame(cmd_id=cmd_id, payload=raw[HDR_SIZE:])


def format_hex(data: bytes, spaced: bool = True) -> str:
    if spaced:
        return " ".join(f"{b:02X}" for b in data)
    return data.hex().upper()


def parse_hex(text: str) -> bytes:
    cleaned = text.replace(" ", "").replace("-", "").replace(":", "").strip()
    if not cleaned:
        return b""
    if len(cleaned) % 2 != 0:
        raise ValueError("odd hex length")
    return bytes.fromhex(cleaned)
