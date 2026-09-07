"""Transport abstraction — ble_local now; dongle later."""

from __future__ import annotations

from abc import ABC, abstractmethod
from collections.abc import Awaitable, Callable
from dataclasses import dataclass

NotifyHandler = Callable[[bytes], Awaitable[None] | None]
DisconnectHandler = Callable[[], Awaitable[None] | None]


class TransportError(RuntimeError):
    pass


@dataclass(frozen=True)
class DeviceInfo:
    address: str
    name: str
    rssi: int | None = None


class Transport(ABC):
    name: str = "base"

    @abstractmethod
    async def scan(
        self,
        timeout_s: float = 5.0,
        name_filter: str = "",
    ) -> list[DeviceInfo]:
        raise NotImplementedError

    @abstractmethod
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
        raise NotImplementedError

    @abstractmethod
    async def disconnect(self) -> None:
        raise NotImplementedError

    @abstractmethod
    async def write(self, data: bytes, *, response: bool = False) -> None:
        raise NotImplementedError

    def max_write_size(self) -> int:
        """Max GATT Write Without Response size; override on real transports."""
        return 20

    @abstractmethod
    def is_connected(self) -> bool:
        raise NotImplementedError
