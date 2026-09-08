"""pywebview JS API + BLE event bridge for the HTML designer."""

from __future__ import annotations

import base64
import queue
import threading
from typing import Any

from nrf_console.app.controller import BleController, UiEvent
from nrf_console.config import AppConfig
from nrf_console.designer.planes import flip_plane_y
from nrf_console.epd.plane import PANEL_PROFILES, geometry_for


def _serialize_event(ev: UiEvent) -> dict[str, Any]:
    """JSON-safe event for the HTML UI (never forward CmdFrame etc.)."""
    data: Any = None
    if ev.kind == "devices" and ev.data is not None:
        data = [
            {"name": d.name, "address": d.address, "rssi": d.rssi}
            for d in ev.data
        ]
    elif ev.kind == "connected":
        data = bool(ev.data)
    elif ev.kind == "busy" and isinstance(ev.data, dict):
        data = {
            "op": str(ev.data.get("op", "idle")),
            "connected": bool(ev.data.get("connected")),
        }
    elif ev.kind == "upload":
        data = bool(ev.data)
    return {"kind": ev.kind, "message": ev.message or "", "data": data}


class DesignerApi:
    """
    Exposed to JS via pywebview js_api.

    Events are queued by a background pump; JS must call ``poll_events()``
    (evaluate_js from non-UI threads is unreliable on Win/WebView2).
    """

    def __init__(self, cfg: AppConfig) -> None:
        self.cfg = cfg
        self.controller = BleController(cfg)
        self._pump_stop = threading.Event()
        self._out: queue.Queue[dict[str, Any]] = queue.Queue(maxsize=512)

    def start(self) -> None:
        self.controller.start()
        threading.Thread(target=self._pump_loop, name="ble-ui-pump", daemon=True).start()

    def stop(self) -> None:
        self._pump_stop.set()
        self.controller.stop()

    def _enqueue(self, payload: dict[str, Any]) -> None:
        try:
            self._out.put_nowait(payload)
        except queue.Full:
            try:
                self._out.get_nowait()
            except queue.Empty:
                pass
            try:
                self._out.put_nowait(payload)
            except queue.Full:
                pass

    def _pump_loop(self) -> None:
        while not self._pump_stop.is_set():
            try:
                self.controller.poll_events(self._on_event, max_n=32)
            except Exception as exc:  # noqa: BLE001
                self._enqueue({"kind": "error", "message": f"pump: {exc}", "data": None})
            self._pump_stop.wait(0.08)

    def _on_event(self, ev: UiEvent) -> None:
        self._enqueue(_serialize_event(ev))

    def poll_events(self) -> list[dict[str, Any]]:
        """Drain pending UI events (called from JS on an interval)."""
        out: list[dict[str, Any]] = []
        while True:
            try:
                out.append(self._out.get_nowait())
            except queue.Empty:
                break
            if len(out) >= 64:
                break
        return out

    def scan(self, name_filter: str = "") -> dict[str, Any]:
        try:
            if name_filter and str(name_filter).strip():
                self.cfg.device_name_filter = str(name_filter).strip()
            self.controller.scan()
            return {"ok": True, "message": "scanning"}
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "error": str(exc)}

    def connect(self, address: str, name: str = "") -> dict[str, Any]:
        try:
            addr = (address or "").strip()
            if not addr:
                return {"ok": False, "error": "empty address"}
            self.controller.connect(addr, name or "")
            return {"ok": True, "message": "connecting"}
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "error": str(exc)}

    def disconnect(self) -> dict[str, Any]:
        try:
            self.controller.disconnect()
            return {"ok": True}
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "error": str(exc)}

    def supported_sizes(self) -> dict[str, Any]:
        """Known firmware panels (hint only; any canvas size may upload)."""
        return {
            "ok": True,
            "sizes": [
                {
                    "width": p.width,
                    "height": p.height,
                    "name": p.name,
                    "flip_y": p.flip_y,
                }
                for p in PANEL_PROFILES
            ],
            "note": "host accepts any size; device DISP_BEGIN rejects mismatch",
        }

    def upload(self, width: int, height: int, bw_b64: str, red_b64: str) -> dict[str, Any]:
        try:
            w, h = int(width), int(height)
            geom = geometry_for(w, h)
        except (TypeError, ValueError) as exc:
            return {"ok": False, "error": str(exc)}
        try:
            bw = base64.b64decode(bw_b64)
            red = base64.b64decode(red_b64) if red_b64 else b"\x00" * geom.plane_size
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "error": f"b64 decode: {exc}"}
        # Known panels with flip_y: map logical top → RAM (firmware panel.flip_y)
        if geom.flip_y:
            try:
                bw = flip_plane_y(bw, w, h)
                red = flip_plane_y(red, w, h)
            except ValueError as exc:
                return {"ok": False, "error": str(exc)}
        if len(bw) != geom.plane_size or len(red) != geom.plane_size:
            return {
                "ok": False,
                "error": (
                    f"plane size bw={len(bw)} red={len(red)} "
                    f"need {geom.plane_size} ({geom.width}x{geom.height})"
                ),
            }
        try:
            self.controller.upload_planes(
                bw, red, label=f"designer-{geom.name}", width=w, height=h
            )
        except Exception as exc:  # noqa: BLE001
            return {"ok": False, "error": str(exc)}
        return {
            "ok": True,
            "message": f"upload queued ({geom.width}x{geom.height} {geom.name})",
        }
