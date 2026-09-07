"""Extensible command registry — add new CMD plugins here."""

from __future__ import annotations

from dataclasses import dataclass

from nrf_console.protocol.cmds import BLE_CMD_DISPLAY, BLE_CMD_PING
from nrf_console.protocol.codec import encode_frame, parse_hex


@dataclass(frozen=True)
class CommandDef:
    key: str
    title: str
    cmd_id: int
    description: str = ""
    default_payload_hex: str = ""

    def build_frame(self, payload_hex: str | None = None) -> bytes:
        text = self.default_payload_hex if payload_hex is None else payload_hex
        payload = parse_hex(text) if text.strip() else b""
        return encode_frame(self.cmd_id, payload)


class CommandRegistry:
    def __init__(self) -> None:
        self._items: dict[str, CommandDef] = {}

    def register(self, cmd: CommandDef) -> None:
        self._items[cmd.key] = cmd

    def get(self, key: str) -> CommandDef:
        return self._items[key]

    def list(self) -> list[CommandDef]:
        return list(self._items.values())


def get_default_registry() -> CommandRegistry:
    reg = CommandRegistry()
    reg.register(
        CommandDef(
            key="ping",
            title="PING",
            cmd_id=BLE_CMD_PING,
            description="Link smoke test — ACK only, no EPD",
        )
    )
    reg.register(
        CommandDef(
            key="display",
            title="DISPLAY",
            cmd_id=BLE_CMD_DISPLAY,
            description="Queue full-refresh EPD demo",
        )
    )
    return reg
