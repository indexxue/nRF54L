"""Reserved: nRF52840 USB dongle transport (not implemented)."""

from __future__ import annotations

from .base import DeviceInfo, DisconnectHandler, NotifyHandler, Transport, TransportError


class DongleTransport(Transport):
    name = "dongle"

    async def scan(
        self,
        timeout_s: float = 5.0,
        name_filter: str = "",
    ) -> list[DeviceInfo]:
        raise TransportError("dongle transport reserved — not implemented yet")

    async def connect(
        self,
        address: str,
        *,
        svc_uuid: str,
        rx_uuid: str,
        tx_uuid: str,
        auto_notify: bool = True,
        on_notify: NotifyHandler | None = None,
        on_disconnect: DisconnectHandler | None = None,
    ) -> None:
        raise TransportError("dongle transport reserved — not implemented yet")

    async def disconnect(self) -> None:
        return

    async def write(self, data: bytes, *, response: bool = False) -> None:
        raise TransportError("dongle transport reserved — not implemented yet")

    def max_write_size(self) -> int:
        return 20

    def is_connected(self) -> bool:
        return False
