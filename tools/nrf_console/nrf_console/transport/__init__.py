from .base import DeviceInfo, Transport, TransportError
from .ble_local import BleLocalTransport
from .dongle import DongleTransport

__all__ = [
    "DeviceInfo",
    "Transport",
    "TransportError",
    "BleLocalTransport",
    "DongleTransport",
]


def create_transport(name: str) -> Transport:
    if name == "ble_local":
        return BleLocalTransport()
    if name == "dongle":
        return DongleTransport()
    raise ValueError(f"unknown transport: {name}")
