"""Async BLE worker bridged to Tk UI thread."""

from __future__ import annotations

import asyncio
import queue
import threading
from collections import defaultdict, deque
from collections.abc import Callable
from dataclasses import dataclass
from typing import Any, Literal

from nrf_console.config import AppConfig
from nrf_console.epd import build_page, iter_upload_steps
from nrf_console.protocol.codec import decode_frame, encode_frame, format_hex
from nrf_console.protocol.cmds import (
    BLE_CMD_ACK,
    BLE_CMD_SLOT_STATUS,
    cmd_name,
    describe_frame,
    parse_ack_payload,
    parse_slot_status,
)
from nrf_console.transport import DeviceInfo, Transport, TransportError, create_transport

OpKind = Literal["idle", "scanning", "connecting", "uploading"]


@dataclass
class UiEvent:
    kind: str
    message: str = ""
    data: Any = None


class BleController:
    def __init__(self, cfg: AppConfig) -> None:
        self.cfg = cfg
        self.events: queue.Queue[UiEvent] = queue.Queue()
        self._loop: asyncio.AbstractEventLoop | None = None
        self._thread: threading.Thread | None = None
        self._transport: Transport | None = None
        self._devices: list[DeviceInfo] = []
        self._ack_waiters: dict[int, deque[asyncio.Future[int]]] = defaultdict(deque)
        self._op: OpKind = "idle"
        self._connected = False

    def start(self) -> None:
        if self._thread and self._thread.is_alive():
            return
        self._thread = threading.Thread(target=self._run_loop, name="ble-loop", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        loop = self._loop
        if loop is None:
            return
        fut = asyncio.run_coroutine_threadsafe(self._shutdown(), loop)
        try:
            # WinRT GattDeviceService.Close can be slow; give it time before killing the loop.
            fut.result(timeout=12)
        except Exception:  # noqa: BLE001
            pass
        loop.call_soon_threadsafe(loop.stop)

    def _run_loop(self) -> None:
        loop = asyncio.new_event_loop()
        asyncio.set_event_loop(loop)
        self._loop = loop
        self._transport = create_transport(self.cfg.transport)
        self._emit("log", f"transport={self.cfg.transport}")
        loop.run_forever()
        pending = asyncio.all_tasks(loop)
        for task in pending:
            task.cancel()
        if pending:
            loop.run_until_complete(asyncio.gather(*pending, return_exceptions=True))
        loop.close()
        self._loop = None

    async def _shutdown(self) -> None:
        self._fail_ack_waiters(RuntimeError("controller stopping"))
        # Always dispose — even half-dead WinRT sessions after link loss.
        if self._transport is not None:
            try:
                await self._transport.disconnect()
            except Exception:  # noqa: BLE001
                pass
        self._connected = False
        self._set_op("idle")

    def _emit(self, kind: str, message: str = "", data: Any = None) -> None:
        self.events.put(UiEvent(kind=kind, message=message, data=data))

    def _submit(self, coro: Any) -> None:
        loop = self._loop
        if loop is None:
            self._emit("error", "BLE loop not ready")
            return
        asyncio.run_coroutine_threadsafe(coro, loop)

    def _set_op(self, op: OpKind) -> None:
        self._op = op
        self._emit("busy", data={"op": op, "connected": self._connected})

    def _try_begin_op(self, op: OpKind) -> bool:
        if self._op != "idle":
            self._emit("error", f"busy ({self._op}) — wait or cancel first")
            return False
        self._set_op(op)
        return True

    def _fail_ack_waiters(self, exc: BaseException) -> None:
        for q in self._ack_waiters.values():
            while q:
                fut = q.popleft()
                if not fut.done():
                    fut.set_exception(exc)
        self._ack_waiters.clear()

    def _resolve_ack(self, req_cmd_id: int, status: int) -> None:
        q = self._ack_waiters.get(req_cmd_id)
        if not q:
            return
        fut = q.popleft()
        if not fut.done():
            fut.set_result(status)

    def scan(self) -> None:
        self._submit(self._scan())

    async def _scan(self) -> None:
        assert self._transport is not None
        if not self._try_begin_op("scanning"):
            return
        was_connected = self._connected
        filt = (self.cfg.device_name_filter or "").strip()
        filt_disp = repr(filt) if filt else "(none)"
        self._emit("status", "scanning…")
        self._emit(
            "log",
            f"scan: timeout={self.cfg.scan_timeout_s:.0f}s×3 "
            f"filter={filt_disp} (active scan on Windows)",
        )
        if was_connected:
            self._emit("log", "scan: disconnecting current link first (Windows radio)")
            self._fail_ack_waiters(RuntimeError("disconnected for scan"))
            self._connected = False
            self._emit("connected", data=False)
        try:
            devices = await self._transport.scan(
                timeout_s=self.cfg.scan_timeout_s,
                name_filter=self.cfg.device_name_filter,
            )
        except Exception as exc:  # noqa: BLE001
            self._emit("error", f"scan failed: {exc}")
            self._emit("status", "idle")
            self._set_op("idle")
            return
        self._devices = devices
        self._emit("devices", data=devices)
        if devices:
            self._emit(
                "log",
                "scan done: "
                + ", ".join(
                    f"{d.name}[{d.address}] rssi={d.rssi}" for d in devices[:8]
                )
                + (f" …(+{len(devices) - 8})" if len(devices) > 8 else ""),
            )
        else:
            tip = (
                " — wait 2–3s after device boot; clear filter; ensure not connected elsewhere"
            )
            self._emit("log", f"scan done: 0 device(s){tip}")
        self._emit("status", "idle")
        self._set_op("idle")

    def connect(self, address: str, name: str = "") -> None:
        self._submit(self._connect(address, name))

    async def _connect(self, address: str, name: str) -> None:
        assert self._transport is not None
        if not self._try_begin_op("connecting"):
            return
        self._emit("status", f"connecting {address}…")
        try:
            await self._transport.connect(
                address,
                svc_uuid=self.cfg.svc_uuid,
                rx_uuid=self.cfg.rx_uuid,
                tx_uuid=self.cfg.tx_uuid,
                auto_notify=self.cfg.auto_notify,
                on_notify=self._on_notify,
                on_disconnect=self._on_disconnect,
            )
        except TransportError as exc:
            self._connected = False
            self._emit("error", str(exc))
            self._emit("status", "idle")
            self._emit("connected", data=False)
            self._set_op("idle")
            return
        self.cfg.last_address = address
        self.cfg.last_name = name
        # Persist SN-derived UUIDs discovered by transport (not eink_tag 53530120 fallback).
        last_svc = getattr(self._transport, "last_svc_uuid", "") or ""
        last_rx = getattr(self._transport, "last_rx_uuid", "") or ""
        last_tx = getattr(self._transport, "last_tx_uuid", "") or ""
        if last_svc and last_rx and last_tx:
            self.cfg.svc_uuid = last_svc
            self.cfg.rx_uuid = last_rx
            self.cfg.tx_uuid = last_tx
            self._emit("log", f"GATT UUID svc={last_svc}")
        max_w = self._transport.max_write_size()
        self._connected = True
        self._emit("connected", data=True)
        self._emit("log", f"connected: {name or address}")
        self._emit("log", f"GATT max write-without-response: {max_w} B")
        self._emit("status", "connected")
        self._set_op("idle")

    def disconnect(self) -> None:
        self._submit(self._disconnect())

    async def _disconnect(self) -> None:
        assert self._transport is not None
        if self._op == "uploading":
            self._emit("error", "busy (uploading) — wait for upload to finish")
            return
        if self._op not in ("idle",):
            self._emit("error", f"busy ({self._op}) — wait")
            return
        self._fail_ack_waiters(RuntimeError("disconnected"))
        await self._transport.disconnect()
        self._connected = False
        self._emit("connected", data=False)
        self._emit("log", "disconnected")
        self._emit("status", "idle")
        self._set_op("idle")

    async def _on_notify(self, data: bytes) -> None:
        hex_s = format_hex(data)
        try:
            frame = decode_frame(data)
            text = describe_frame(frame)
            self._emit("rx", f"← {text}  [{hex_s}]", data=frame)
            if frame.cmd_id == BLE_CMD_ACK:
                try:
                    ack = parse_ack_payload(frame.payload)
                    self._resolve_ack(int(ack["req_cmd_id"]), int(ack["status"]))
                except ValueError:
                    pass
            elif frame.cmd_id == BLE_CMD_SLOT_STATUS:
                try:
                    self._emit("slot_status", data=parse_slot_status(frame.payload))
                except ValueError:
                    pass
        except ValueError as exc:
            self._emit("rx", f"← raw [{hex_s}] ({exc})")

    async def _on_disconnect(self) -> None:
        self._fail_ack_waiters(RuntimeError("link lost"))
        self._connected = False
        self._emit("connected", data=False)
        self._emit("log", "link lost (device or host) — wait ~1s then Scan before Connect")
        self._emit("status", "idle")
        # If upload/connect was mid-flight, unlock UI; scan's own finally path also clears.
        if self._op != "scanning":
            self._set_op("idle")

    def send_raw(self, data: bytes) -> None:
        self._submit(self._send_raw(data))

    async def _send_raw(self, data: bytes) -> None:
        assert self._transport is not None
        if self._op == "uploading":
            self._emit("error", "busy (uploading) — commands blocked until done")
            return
        if self._op != "idle":
            self._emit("error", f"busy ({self._op}) — wait")
            return
        if not self._connected or not self._transport.is_connected():
            self._emit("error", "not connected")
            return
        try:
            await self._transport.write(data)
        except TransportError as exc:
            self._emit("error", str(exc))
            return
        self._emit("tx", f"→ [{format_hex(data)}]")

    def send_frame(self, cmd_id: int, payload: bytes = b"") -> None:
        self.send_raw(encode_frame(cmd_id, payload))

    async def _send_wait_ack(
        self,
        cmd_id: int,
        payload: bytes = b"",
        *,
        timeout_s: float = 5.0,
    ) -> int:
        assert self._transport is not None
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[int] = loop.create_future()
        self._ack_waiters[cmd_id].append(fut)
        frame = encode_frame(cmd_id, payload)
        try:
            await self._transport.write(frame)
        except TransportError:
            q = self._ack_waiters[cmd_id]
            if fut in q:
                q.remove(fut)
            raise
        self._emit("tx", f"→ {cmd_name(cmd_id)}({cmd_id}) plen={len(payload)}")
        try:
            return await asyncio.wait_for(fut, timeout=timeout_s)
        except asyncio.TimeoutError:
            q = self._ack_waiters[cmd_id]
            if fut in q:
                q.remove(fut)
            raise TimeoutError(f"ACK timeout for {cmd_name(cmd_id)}({cmd_id})") from None

    def upload_smoke(self) -> None:
        self.upload_page("smoke")

    def upload_page(self, page_key: str) -> None:
        self._submit(self._upload_page(page_key))

    def upload_planes(
        self,
        bw: bytes,
        red: bytes | None,
        *,
        label: str = "custom",
        width: int | None = None,
        height: int | None = None,
        mode: int | None = None,
        page_id: int = 0,
        show: bool = True,
    ) -> None:
        self._submit(
            self._upload_planes(
                bw,
                red,
                label=label,
                width=width,
                height=height,
                mode=mode,
                page_id=page_id,
                show=show,
            )
        )

    async def _upload_page(self, page_key: str) -> None:
        try:
            bw, red = build_page(page_key)
        except KeyError as exc:
            self._emit("error", str(exc))
            return
        await self._upload_planes(bw, red, label=page_key)

    async def _upload_planes(
        self,
        bw: bytes,
        red: bytes | None,
        *,
        label: str = "custom",
        width: int | None = None,
        height: int | None = None,
        mode: int | None = None,
        page_id: int = 0,
        show: bool = True,
    ) -> None:
        if self._transport is None or not self._transport.is_connected() or not self._connected:
            self._emit("error", "not connected")
            return
        if not self._try_begin_op("uploading"):
            return

        self._emit("status", f"uploading EPD ({label})…")
        try:
            max_frame = self._transport.max_write_size()
            # CMD frame = 4 hdr + payload; CHUNK payload = 5 hdr + data
            payload_cap = max_frame - 4
            need_begin = 20 if mode is not None else 18
            if payload_cap < need_begin:
                raise RuntimeError(
                    f"ATT write max {max_frame} B too small for DISP_BEGIN "
                    f"(need ≥{need_begin + 4}). "
                    "Rebuild firmware with larger BT_L2CAP_TX_MTU and reflash."
                )
            chunk_data_max = min(235, max(1, payload_cap - 5))
            steps = list(
                iter_upload_steps(
                    bw,
                    red,
                    width=width,
                    height=height,
                    chunk_data_max=chunk_data_max,
                    mode=mode,
                    page_id=page_id,
                    show=show,
                )
            )
            total = len(steps)
            geom = f"{width}x{height}" if width and height else f"plane={len(bw)}"
            begin_plen = len(steps[0][1]) if steps else 0
            slot_info = (
                f" BEGIN={begin_plen}B mode={mode} page={page_id} show={int(show)}"
                if mode is not None
                else f" BEGIN={begin_plen}B"
            )
            self._emit(
                "log",
                f"EPD upload [{label}] {geom}: {total} frames "
                f"(BEGIN + {total - 2} CHUNK + REFRESH), "
                f"chunk_data≤{chunk_data_max}, wor≤{max_frame},{slot_info}",
            )
            for i, (cmd_id, payload) in enumerate(steps, start=1):
                if not self._connected:
                    raise RuntimeError("link lost during upload")
                status = await self._send_wait_ack(cmd_id, payload, timeout_s=8.0)
                if status != 0:
                    self._emit(
                        "error",
                        f"upload abort at {i}/{total}: "
                        f"{cmd_name(cmd_id)} status={status} "
                        f"(HINT: -6=device busy; wait for refresh then retry)",
                    )
                    self._emit("status", "connected" if self._connected else "idle")
                    return
                if i == 1 or i == total or (i % 20) == 0:
                    self._emit(
                        "log",
                        f"upload {i}/{total} {cmd_name(cmd_id)} ACK status={status}",
                    )
            self._emit(
                "log",
                f"EPD upload [{label}] done — panel refresh runs on device",
            )
            self._emit("status", "connected" if self._connected else "idle")
        except Exception as exc:  # noqa: BLE001
            self._emit("error", f"upload failed: {exc}")
            self._emit("status", "connected" if self._connected else "idle")
        finally:
            if self._op == "uploading":
                self._set_op("idle")

    def poll_events(self, handler: Callable[[UiEvent], None], max_n: int = 64) -> None:
        for _ in range(max_n):
            try:
                ev = self.events.get_nowait()
            except queue.Empty:
                break
            handler(ev)
