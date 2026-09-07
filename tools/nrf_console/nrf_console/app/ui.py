"""EPD Tag Designer main window."""

from __future__ import annotations

from tkinter import messagebox

import customtkinter as ctk

from nrf_console.commands import get_default_registry
from nrf_console.config import AppConfig, save_config
from nrf_console.epd import build_page
from nrf_console.epd.image_import import image_to_planes
from nrf_console.transport import DeviceInfo

from .advanced_dialog import AdvancedDialog
from .controller import BleController, UiEvent
from .editor_panel import EditorPanel
from .log_panel import LogPanel
from .preview_panel import PreviewPanel
from .topbar import TopBar

APP_TITLE = "EPD Tag Designer"
_PREVIEW_DEBOUNCE_MS = 150


class ConsoleApp(ctk.CTk):
    def __init__(self, cfg: AppConfig) -> None:
        super().__init__()
        self.cfg = cfg
        self.controller = BleController(cfg)
        self.registry = get_default_registry()
        self._devices: list[DeviceInfo] = []
        self._connected = False
        self._op = "idle"
        self._preview_after: str | None = None
        self._planes: tuple[bytes, bytes] | None = None
        self._advanced: AdvancedDialog | None = None

        ctk.set_appearance_mode(cfg.appearance_mode)
        ctk.set_default_color_theme(cfg.color_theme)

        self.title(APP_TITLE)
        self.geometry(cfg.window_geometry)
        self.minsize(980, 720)

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.controller.start()
        self.after(100, self._pump)
        self._refresh_controls()
        self._schedule_preview()
        self.log.append("ready — scan eink_tag, connect, design, Upload")

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        self.topbar = TopBar(
            self,
            filter_text=self.cfg.device_name_filter,
            on_scan=self._on_scan,
            on_connect=self._on_connect,
            on_disconnect=self._on_disconnect,
            on_advanced=self._on_advanced,
        )
        self.topbar.grid(row=0, column=0, sticky="ew", padx=14, pady=(14, 8))

        main = ctk.CTkFrame(self, fg_color="transparent")
        main.grid(row=1, column=0, sticky="nsew", padx=14, pady=4)
        main.grid_columnconfigure(0, weight=1)
        main.grid_columnconfigure(1, weight=0)
        main.grid_rowconfigure(0, weight=1)

        self.editor = EditorPanel(
            main,
            on_change=self._schedule_preview,
            on_upload=self._on_upload,
        )
        self.editor.grid(row=0, column=0, sticky="nsew", padx=(0, 8))

        self.preview = PreviewPanel(main, scale=2)
        self.preview.grid(row=0, column=1, sticky="ns")

        self.log = LogPanel(
            self,
            on_clear=lambda: self.log.clear(),
            on_save_cfg=self._save_cfg,
        )
        self.log.grid(row=2, column=0, sticky="ew", padx=14, pady=(8, 14))

    def _schedule_preview(self) -> None:
        if self._preview_after is not None:
            try:
                self.after_cancel(self._preview_after)
            except Exception:  # noqa: BLE001
                pass
        self._preview_after = self.after(_PREVIEW_DEBOUNCE_MS, self._refresh_preview)

    def _refresh_preview(self) -> None:
        self._preview_after = None
        try:
            bw, red = self._compose_planes()
            self._planes = (bw, red)
            self.preview.show_planes(bw, red, master=self)
        except Exception as exc:  # noqa: BLE001
            self.log.append(f"preview failed: {exc}")

    def _compose_planes(self) -> tuple[bytes, bytes]:
        if self.editor.mode() == "image":
            path = self.editor.image_path()
            if path is None:
                # Empty white canvas placeholder
                return build_page("tag", title="(image)", price="—", sku="pick a file")
            return image_to_planes(
                path,
                luminance_threshold=self.editor.luminance_threshold(),
            )
        key = self.editor.layout_key()
        fields = self.editor.tag_fields()
        return build_page(key, **fields)

    def _widget_ok(self) -> bool:
        return self._op == "idle" and self._connected

    def _refresh_controls(self) -> None:
        idle = self._op == "idle"
        connected = self._connected
        self.topbar.refresh_states(
            idle=idle, connected=connected, has_devices=bool(self._devices)
        )
        self.editor.refresh_states(
            can_edit=idle,
            can_upload=idle and connected and self._op != "uploading",
        )

    def _on_scan(self) -> None:
        if self._op != "idle":
            messagebox.showinfo(APP_TITLE, f"Busy ({self._op}).")
            return
        if self._connected:
            if not messagebox.askokcancel(
                APP_TITLE,
                "Scan will disconnect the current link (Windows BLE needs a free radio).\nContinue?",
            ):
                return
        self.cfg.device_name_filter = self.topbar.filter_entry.get().strip()
        self.controller.scan()

    def _on_connect(self) -> None:
        if self._op != "idle":
            messagebox.showinfo(APP_TITLE, f"Busy ({self._op}).")
            return
        if self._connected:
            messagebox.showinfo(APP_TITLE, "Already connected — disconnect first.")
            return
        if not self._devices:
            messagebox.showinfo(APP_TITLE, "Scan first.")
            return
        label = self.topbar.device_menu.get()
        idx = self._label_index(label)
        if idx is None:
            messagebox.showinfo(APP_TITLE, "Pick a device.")
            return
        dev = self._devices[idx]
        self.controller.connect(dev.address, dev.name)

    def _on_disconnect(self) -> None:
        if self._op == "uploading":
            messagebox.showinfo(APP_TITLE, "Upload in progress — wait for it to finish.")
            return
        if self._op != "idle":
            messagebox.showinfo(APP_TITLE, f"Busy ({self._op}).")
            return
        self.controller.disconnect()

    def _on_upload(self) -> None:
        if not self._widget_ok():
            messagebox.showinfo(APP_TITLE, "Connect first and wait until idle.")
            return
        try:
            bw, red = self._compose_planes()
            self._planes = (bw, red)
            self.preview.show_planes(bw, red, master=self)
        except Exception as exc:  # noqa: BLE001
            messagebox.showerror(APP_TITLE, f"Compose failed: {exc}")
            return
        if self.editor.mode() == "image" and self.editor.image_path() is None:
            messagebox.showinfo(APP_TITLE, "Browse an image first.")
            return
        label = (
            f"image:{self.editor.image_path().name}"
            if self.editor.mode() == "image" and self.editor.image_path()
            else self.editor.layout_key()
        )
        self.controller.upload_planes(bw, red, label=label)

    def _on_advanced(self) -> None:
        if self._advanced is not None and self._advanced.winfo_exists():
            self._advanced.focus()
            return
        self._advanced = AdvancedDialog(
            self,
            registry=self.registry,
            on_send_raw=self.controller.send_raw,
            can_send=self._widget_ok,
        )

    def _label_index(self, label: str) -> int | None:
        for i, d in enumerate(self._devices):
            if self._device_label(d) == label:
                return i
        return None

    @staticmethod
    def _device_label(d: DeviceInfo) -> str:
        rssi = f"{d.rssi} dBm" if d.rssi is not None else "?"
        return f"{d.name}  [{d.address}]  ({rssi})"

    def _save_cfg(self) -> None:
        self.cfg.device_name_filter = self.topbar.filter_entry.get().strip()
        self.cfg.window_geometry = self.geometry()
        path = save_config(self.cfg)
        self.log.append(f"config saved: {path}")

    def _handle_event(self, ev: UiEvent) -> None:
        if ev.kind in ("log", "tx", "rx"):
            self.log.append(ev.message)
        elif ev.kind == "error":
            self.log.append(f"ERROR: {ev.message}")
        elif ev.kind == "status":
            self.topbar.set_status(ev.message)
        elif ev.kind == "busy":
            data = ev.data or {}
            self._op = str(data.get("op", "idle"))
            if "connected" in data:
                self._connected = bool(data["connected"])
            self._refresh_controls()
        elif ev.kind == "devices":
            self._devices = list(ev.data or [])
            if not self._devices:
                self.topbar.set_devices([], [], "(no devices)")
            else:
                labels = [self._device_label(d) for d in self._devices]
                pick = labels[0]
                if self.cfg.last_address:
                    for i, d in enumerate(self._devices):
                        if d.address == self.cfg.last_address:
                            pick = labels[i]
                            break
                self.topbar.set_devices(self._devices, labels, pick)
            self._refresh_controls()
        elif ev.kind == "connected":
            self._connected = bool(ev.data)
            self._refresh_controls()
        elif ev.kind == "upload":
            self._op = "uploading" if ev.data else "idle"
            self._refresh_controls()

    def _pump(self) -> None:
        self.controller.poll_events(self._handle_event)
        self.after(80, self._pump)

    def _on_close(self) -> None:
        try:
            self.cfg.device_name_filter = self.topbar.filter_entry.get().strip()
            self.cfg.window_geometry = self.geometry()
            save_config(self.cfg)
        except Exception:  # noqa: BLE001
            pass
        self.controller.stop()
        self.destroy()


def run_app(cfg: AppConfig) -> None:
    app = ConsoleApp(cfg)
    app.mainloop()
