"""Windows / host BLE via bleak (local adapter)."""

from __future__ import annotations

import asyncio
import sys

from bleak import BleakClient, BleakScanner
from bleak.backends.characteristic import BleakGATTCharacteristic
from bleak.backends.device import BLEDevice
from bleak.backends.scanner import AdvertisementData

from .base import DeviceInfo, DisconnectHandler, NotifyHandler, Transport, TransportError

# ATT default: MTU 23 → max write-without-response payload 20.
_DEFAULT_WOR = 20
_CONNECT_ATTEMPTS = 3
_CONNECT_TIMEOUT_S = 20.0


class BleLocalTransport(Transport):
    name = "ble_local"

    def __init__(self) -> None:
        self._client: BleakClient | None = None
        self._rx_uuid: str = ""
        self._tx_uuid: str = ""
        self._on_notify: NotifyHandler | None = None
        self._on_disconnect: DisconnectHandler | None = None
        self._max_wor: int = _DEFAULT_WOR
        # One radio lock for scan + connect — WinRT hates overlapping watchers/sessions.
        self._radio_lock = asyncio.Lock()
        # Prefer BLEDevice from last scan; address-only reconnect is flaky on Windows.
        self._ble_devices: dict[str, BLEDevice] = {}

    async def scan(
        self,
        timeout_s: float = 5.0,
        name_filter: str = "",
    ) -> list[DeviceInfo]:
        """Scan for peripherals.

        Windows adapters often fail or return empty while a GATT connection is
        open, and long-lived watcher state can go stale. Always drop any link
        first, then run a fresh scanner with one retry on empty/error.
        """
        async with self._radio_lock:
            if self._client is not None:
                await self._dispose_client(settle_s=0.4)

            last_exc: Exception | None = None
            for attempt in range(2):
                try:
                    devices = await self._scan_once(timeout_s, name_filter)
                except Exception as exc:  # noqa: BLE001
                    last_exc = exc
                    await asyncio.sleep(0.5)
                    continue
                if devices or attempt == 1:
                    return devices
                # Empty first pass: recreate watcher after a short pause.
                await asyncio.sleep(0.5)

            if last_exc is not None:
                raise TransportError(f"scan failed: {last_exc}") from last_exc
            return []

    async def _scan_once(
        self,
        timeout_s: float,
        name_filter: str,
    ) -> list[DeviceInfo]:
        found: dict[str, DeviceInfo] = {}
        found_dev: dict[str, BLEDevice] = {}
        scanner: BleakScanner | None = None

        def _cb(device: BLEDevice, adv: AdvertisementData) -> None:
            name = device.name or adv.local_name or ""
            if name_filter and name_filter.lower() not in name.lower():
                return
            rssi = adv.rssi
            prev = found.get(device.address)
            if prev is None or (rssi is not None and (prev.rssi is None or rssi > prev.rssi)):
                found[device.address] = DeviceInfo(
                    address=device.address,
                    name=name or "(no name)",
                    rssi=rssi,
                )
                found_dev[device.address] = device

        try:
            scanner = BleakScanner(detection_callback=_cb)
            await scanner.start()
            await asyncio.sleep(timeout_s)
        finally:
            if scanner is not None:
                try:
                    await scanner.stop()
                except Exception:  # noqa: BLE001
                    pass
            # Brief settle so the next start()/connect() is not racing WinRT.
            await asyncio.sleep(0.15)

        self._ble_devices.update(found_dev)
        return sorted(
            found.values(),
            key=lambda d: (d.rssi is None, -(d.rssi or 0), d.name.lower()),
        )

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
        async with self._radio_lock:
            # Always tear down any prior client (including half-dead after link loss).
            if self._client is not None:
                await self._dispose_client(settle_s=0.5)

            self._rx_uuid = rx_uuid
            self._tx_uuid = tx_uuid
            self._on_notify = on_notify
            self._on_disconnect = on_disconnect
            self._max_wor = _DEFAULT_WOR

            last_exc: Exception | None = None
            client: BleakClient | None = None

            for attempt in range(_CONNECT_ATTEMPTS):
                # First tries use cached BLEDevice from scan; later tries force rediscovery.
                if attempt == 0:
                    target: BLEDevice | str = self._ble_devices.get(address) or address
                else:
                    self._ble_devices.pop(address, None)
                    target = address

                client = BleakClient(
                    target,
                    disconnected_callback=self._disconnected,
                    timeout=_CONNECT_TIMEOUT_S,
                )
                try:
                    await client.connect()
                    break
                except Exception as exc:  # noqa: BLE001 — surface / retry
                    last_exc = exc
                    try:
                        await client.disconnect()
                    except Exception:  # noqa: BLE001
                        pass
                    client = None
                    await asyncio.sleep(0.5 * (attempt + 1))
            else:
                self._on_notify = None
                self._on_disconnect = None
                raise TransportError(
                    f"connect failed after {_CONNECT_ATTEMPTS} attempts: {last_exc}"
                ) from last_exc

            assert client is not None
            self._client = client

            # Best-effort service check (some Windows stacks are picky about UUID lookup).
            try:
                svc = client.services.get_service(svc_uuid)
                if svc is None:
                    await self._dispose_client(settle_s=0.3)
                    raise TransportError(f"service not found: {svc_uuid}")
            except TransportError:
                raise
            except Exception:
                pass

            if auto_notify and on_notify is not None:
                try:
                    await client.start_notify(tx_uuid, self._notify_handler)
                except Exception as exc:  # noqa: BLE001
                    await self._dispose_client(settle_s=0.3)
                    raise TransportError(f"enable notify failed: {exc}") from exc

            await self._refresh_max_wor(wait_s=3.0)
            if self._client is not client or not client.is_connected:
                self._client = None
                self._max_wor = _DEFAULT_WOR
                self._on_notify = None
                self._on_disconnect = None
                raise TransportError("link lost during connect setup")

    def _notify_handler(self, _char: BleakGATTCharacteristic, data: bytearray) -> None:
        handler = self._on_notify
        if handler is None:
            return
        result = handler(bytes(data))
        if asyncio.iscoroutine(result):
            asyncio.create_task(result)

    def _disconnected(self, client: BleakClient) -> None:
        # Drop stale handle so the next connect() does not skip dispose.
        if self._client is client:
            self._client = None
            self._max_wor = _DEFAULT_WOR
        handler = self._on_disconnect
        if handler is None:
            return
        result = handler()
        if asyncio.iscoroutine(result):
            asyncio.create_task(result)

    def _read_reported_wor(self) -> int:
        client = self._client
        if client is None:
            return _DEFAULT_WOR
        try:
            char = client.services.get_characteristic(self._rx_uuid)
            if char is not None:
                return max(_DEFAULT_WOR, int(char.max_write_without_response_size))
        except Exception:  # noqa: BLE001
            pass
        return _DEFAULT_WOR

    def _read_backend_pdu(self) -> int | None:
        """Windows: session max_pdu_size often updates after char enumeration."""
        client = self._client
        if client is None:
            return None
        try:
            backend = client._backend  # noqa: SLF001 — bleak WinRT quirk
            session = getattr(backend, "_session", None)
            if session is None:
                return None
            pdu = getattr(session, "max_pdu_size", None)
            if pdu is None:
                return None
            pdu_i = int(pdu)
            return pdu_i if pdu_i >= 23 else None
        except Exception:  # noqa: BLE001
            return None

    def _patch_char_wor(self, size: int) -> None:
        client = self._client
        if client is None:
            return
        try:
            char = client.services.get_characteristic(self._rx_uuid)
            if char is not None and hasattr(char, "_max_write_without_response_size"):
                char._max_write_without_response_size = size  # noqa: SLF001
        except Exception:  # noqa: BLE001
            pass

    async def _refresh_max_wor(self, wait_s: float = 3.0) -> None:
        """Wait for MTU / PDU update (common Windows race), then cache max write size."""
        deadline = asyncio.get_running_loop().time() + wait_s
        best = self._read_reported_wor()
        while asyncio.get_running_loop().time() < deadline:
            reported = self._read_reported_wor()
            pdu = self._read_backend_pdu()
            from_pdu = (pdu - 3) if pdu is not None else reported
            best = max(best, reported, from_pdu)
            if best >= 100:
                break
            await asyncio.sleep(0.25)

        if best <= _DEFAULT_WOR:
            pdu = self._read_backend_pdu()
            if pdu is not None and pdu > 23:
                best = pdu - 3

        self._max_wor = max(_DEFAULT_WOR, best)
        if self._max_wor > _DEFAULT_WOR:
            self._patch_char_wor(self._max_wor)

    def max_write_size(self) -> int:
        """Max bytes for one GATT Write Without Response."""
        return self._max_wor

    async def _dispose_client(self, *, settle_s: float = 0.35) -> None:
        """Force-close GATT session even if is_connected() is already False."""
        client = self._client
        self._client = None
        self._max_wor = _DEFAULT_WOR
        self._on_notify = None
        # Clear before disconnect so intentional tear-down does not emit "link lost".
        self._on_disconnect = None
        if client is None:
            return
        try:
            if client.is_connected and self._tx_uuid:
                try:
                    await client.stop_notify(self._tx_uuid)
                except Exception:  # noqa: BLE001
                    pass
            # Always call disconnect() — WinRT may still hold a half-open session.
            await client.disconnect()
        except Exception:  # noqa: BLE001
            pass
        if settle_s > 0:
            await asyncio.sleep(settle_s)

    async def disconnect(self) -> None:
        async with self._radio_lock:
            await self._dispose_client(settle_s=0.35)

    async def write(self, data: bytes, *, response: bool = False) -> None:
        """Prefer Write Without Response (firmware RX supports it).

        Write With Response of large values on default MTU may trigger ATT
        Prepare Write; with ``CONFIG_BT_ATT_PREPARE_COUNT=0`` the peer returns
        Request Not Supported. Keep frames ≤ ``max_write_size()``.
        """
        client = self._client
        if client is None or not client.is_connected:
            raise TransportError("not connected")

        if len(data) > self._max_wor:
            # One more refresh in case MTU landed late.
            await self._refresh_max_wor(wait_s=1.0)
            if len(data) > self._max_wor:
                raise TransportError(
                    f"frame {len(data)} B exceeds max write {self._max_wor} B "
                    f"(raise device ATT MTU / rebuild firmware)"
                )

        try:
            await client.write_gatt_char(self._rx_uuid, data, response=False)
        except OSError as exc:
            # Windows E_INVALIDARG when WoR size still wrong — last resort.
            if sys.platform == "win32" and len(data) <= 512:
                try:
                    await client.write_gatt_char(self._rx_uuid, data, response=True)
                    return
                except Exception as exc2:  # noqa: BLE001
                    raise TransportError(f"write failed: {exc2}") from exc2
            raise TransportError(f"write failed: {exc}") from exc
        except Exception as exc:  # noqa: BLE001
            raise TransportError(f"write failed: {exc}") from exc

    def is_connected(self) -> bool:
        return bool(self._client and self._client.is_connected)
