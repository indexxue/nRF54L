"""ink_panel visual console — BLE + 4.2\" preview + slot targeting."""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from tkinter import filedialog

import customtkinter as ctk

from ink_panel_console.app.ble_bridge import (
    BLE_CMD_PING,
    BleController,
    UiEvent,
    sync_back,
    to_app_config,
)
from ink_panel_console.app.preview import PreviewPanel
from ink_panel_console.config import (
    MODE_HOME,
    MODE_INFO,
    MODE_NAMES,
    MODE_NOTES,
    PANEL_HEIGHT,
    PANEL_WIDTH,
    InkPanelConfig,
    load_config,
    save_config,
    write_nrf_console_bridge,
)
from ink_panel_console.pages import PAGE_BUILDERS, PAGE_DEFAULT_SLOT
from ink_panel_console.panel import image_to_planes
from ink_panel_console.paths import nrf_console_root

APP_TITLE = "ink_panel Console"
_PAPER = "#e8e2d4"
_SHELL = "#2b2a27"
_RED = "#c23b2a"
_MUTED = "#9a9486"


class ConsoleApp(ctk.CTk):
    def __init__(self, cfg: InkPanelConfig) -> None:
        super().__init__()
        self.ink_cfg = cfg
        self.app_cfg = to_app_config(cfg)
        self.controller = BleController(self.app_cfg)
        self._devices: list = []
        self._connected = False
        self._op = "idle"
        self._planes: tuple[bytes, bytes] | None = None
        self._mode = MODE_HOME
        self._page_id = 0
        self._template = "home"

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("dark-blue")

        self.title(APP_TITLE)
        self.geometry(self.app_cfg.window_geometry)
        self.minsize(1080, 720)
        self.configure(fg_color=_SHELL)

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.controller.start()
        self.after(80, self._pump)
        self._select_template("home")
        self._log("ready — Scan → Connect → 选模/页 → Upload（α 落 Flash home/0）")

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        # —— top bar ——
        top = ctk.CTkFrame(self, fg_color="#1f1e1c", corner_radius=0)
        top.grid(row=0, column=0, sticky="ew")
        top.grid_columnconfigure(3, weight=1)

        ctk.CTkLabel(
            top,
            text="ink_panel",
            font=ctk.CTkFont(family="Segoe UI", size=18, weight="bold"),
            text_color=_PAPER,
        ).grid(row=0, column=0, padx=(16, 8), pady=12)

        ctk.CTkLabel(
            top,
            text=f"{PANEL_WIDTH}×{PANEL_HEIGHT} BWR · Flash 页槽",
            font=ctk.CTkFont(size=12),
            text_color=_MUTED,
        ).grid(row=0, column=1, padx=4, pady=12, sticky="w")

        self.filter_entry = ctk.CTkEntry(top, width=120, placeholder_text="filter")
        self.filter_entry.insert(0, self.ink_cfg.device_name_filter)
        self.filter_entry.grid(row=0, column=2, padx=8, pady=12)

        self.btn_scan = ctk.CTkButton(top, text="Scan", width=72, command=self._on_scan)
        self.btn_scan.grid(row=0, column=3, padx=4, pady=12, sticky="e")

        self.device_menu = ctk.CTkOptionMenu(top, values=["(scan first)"], width=220)
        self.device_menu.grid(row=0, column=4, padx=4, pady=12)

        self.btn_conn = ctk.CTkButton(top, text="Connect", width=88, command=self._on_connect)
        self.btn_conn.grid(row=0, column=5, padx=4, pady=12)

        self.btn_disc = ctk.CTkButton(
            top, text="Disconnect", width=96, fg_color="#444", command=self._on_disconnect
        )
        self.btn_disc.grid(row=0, column=6, padx=4, pady=12)

        self.btn_ping = ctk.CTkButton(top, text="PING", width=64, command=self._on_ping)
        self.btn_ping.grid(row=0, column=7, padx=(4, 16), pady=12)

        # —— main ——
        main = ctk.CTkFrame(self, fg_color="transparent")
        main.grid(row=1, column=0, sticky="nsew", padx=14, pady=10)
        main.grid_columnconfigure(1, weight=1)
        main.grid_rowconfigure(0, weight=1)

        left = ctk.CTkFrame(main, fg_color="#35332f", corner_radius=12, width=300)
        left.grid(row=0, column=0, sticky="ns", padx=(0, 10))
        left.grid_propagate(False)

        ctk.CTkLabel(
            left, text="模式 / 页槽", font=ctk.CTkFont(size=14, weight="bold"), text_color=_PAPER
        ).pack(anchor="w", padx=14, pady=(14, 6))

        mode_row = ctk.CTkFrame(left, fg_color="transparent")
        mode_row.pack(fill="x", padx=12, pady=4)
        self.mode_var = ctk.StringVar(value="home")
        for label, mid in (("首页", "home"), ("记事", "notes"), ("信息", "info")):
            ctk.CTkRadioButton(
                mode_row,
                text=label,
                variable=self.mode_var,
                value=mid,
                command=self._on_mode_change,
                text_color=_PAPER,
            ).pack(side="left", padx=4)

        page_row = ctk.CTkFrame(left, fg_color="transparent")
        page_row.pack(fill="x", padx=12, pady=8)
        ctk.CTkLabel(page_row, text="page_id", text_color=_MUTED).pack(side="left")
        self.page_spin = ctk.CTkEntry(page_row, width=56)
        self.page_spin.insert(0, "0")
        self.page_spin.pack(side="left", padx=8)

        ctk.CTkLabel(
            left, text="画面来源", font=ctk.CTkFont(size=14, weight="bold"), text_color=_PAPER
        ).pack(anchor="w", padx=14, pady=(16, 6))

        for key, title in (
            ("home", "模板 · 首页"),
            ("notes", "模板 · 记事"),
            ("info", "模板 · 信息牌"),
            ("bands", "模板 · 三色条"),
        ):
            ctk.CTkButton(
                left,
                text=title,
                anchor="w",
                fg_color="#4a4740",
                hover_color=_RED,
                command=lambda k=key: self._select_template(k),
            ).pack(fill="x", padx=12, pady=3)

        ctk.CTkButton(
            left,
            text="导入图片…",
            fg_color="#5a4038",
            hover_color=_RED,
            command=self._on_import_image,
        ).pack(fill="x", padx=12, pady=(10, 4))

        ctk.CTkButton(
            left,
            text="上传到设备",
            height=40,
            font=ctk.CTkFont(size=15, weight="bold"),
            fg_color=_RED,
            hover_color="#a83224",
            command=self._on_upload,
        ).pack(fill="x", padx=12, pady=(16, 8))

        ctk.CTkButton(
            left,
            text="打开 DIY 设计器 (nrf_console)",
            fg_color="#3d4a3a",
            hover_color="#2f6b3a",
            command=self._on_designer,
        ).pack(fill="x", padx=12, pady=(4, 14))

        self.slot_hint = ctk.CTkLabel(
            left,
            text="",
            text_color=_MUTED,
            font=ctk.CTkFont(size=11),
            wraplength=260,
            justify="left",
        )
        self.slot_hint.pack(anchor="w", padx=14, pady=(0, 14))
        self._refresh_slot_hint()

        # preview
        self.preview = PreviewPanel(main, scale=2)
        self.preview.grid(row=0, column=1, sticky="nsew")

        # log
        bottom = ctk.CTkFrame(self, fg_color="#1f1e1c", corner_radius=0)
        bottom.grid(row=2, column=0, sticky="ew")
        bottom.grid_columnconfigure(0, weight=1)

        self.status = ctk.CTkLabel(bottom, text="idle", text_color=_MUTED, anchor="w")
        self.status.grid(row=0, column=0, sticky="ew", padx=16, pady=(8, 0))

        self.log = ctk.CTkTextbox(bottom, height=120, fg_color="#151413", text_color=_PAPER)
        self.log.grid(row=1, column=0, sticky="ew", padx=12, pady=(4, 12))

        self._refresh_controls()

    def _log(self, msg: str) -> None:
        self.log.insert("end", msg + "\n")
        self.log.see("end")

    def _refresh_slot_hint(self) -> None:
        mode = self._mode_from_ui()
        page = self._page_from_ui()
        name = MODE_NAMES.get(mode, str(mode))
        if self.ink_cfg.begin_with_slot:
            self.slot_hint.configure(
                text=f"β BEGIN → mode={name} page={page}（写槽并可切显）"
            )
        else:
            self.slot_hint.configure(
                text=f"目标 UI：{name}/{page} · α 固件实际写入 Flash home/0"
            )

    def _mode_from_ui(self) -> int:
        m = self.mode_var.get()
        if m == "notes":
            return MODE_NOTES
        if m == "info":
            return MODE_INFO
        return MODE_HOME

    def _page_from_ui(self) -> int:
        try:
            return max(0, int(self.page_spin.get().strip() or "0"))
        except ValueError:
            return 0

    def _on_mode_change(self) -> None:
        self._mode = self._mode_from_ui()
        self._refresh_slot_hint()

    def _select_template(self, key: str) -> None:
        self._template = key
        bw, red = PAGE_BUILDERS[key]()
        self._planes = (bw, red)
        mode, page = PAGE_DEFAULT_SLOT[key]
        self.mode_var.set(MODE_NAMES[mode])
        self.page_spin.delete(0, "end")
        self.page_spin.insert(0, str(page))
        self._mode = mode
        self._page_id = page
        self.preview.show_planes(bw, red, master=self)
        self._refresh_slot_hint()
        self._log(f"template={key} preview ready")

    def _on_import_image(self) -> None:
        path = filedialog.askopenfilename(
            title="导入图片 → 400×300 BWR",
            filetypes=[
                ("Images", "*.png;*.jpg;*.jpeg;*.bmp;*.webp"),
                ("All", "*.*"),
            ],
        )
        if not path:
            return
        try:
            bw, red = image_to_planes(path)
        except Exception as exc:  # noqa: BLE001
            self._log(f"import fail: {exc}")
            return
        self._planes = (bw, red)
        self.preview.show_planes(bw, red, master=self)
        self._log(f"imported {Path(path).name}")

    def _on_scan(self) -> None:
        self.app_cfg.device_name_filter = self.filter_entry.get().strip() or "ink_panel"
        self.ink_cfg.device_name_filter = self.app_cfg.device_name_filter
        self.controller.scan()

    def _on_connect(self) -> None:
        label = self.device_menu.get()
        addr = self._address_from_label(label)
        if not addr:
            self._log("select a device from the list after Scan")
            return
        name = label.split("  ")[0] if "  " in label else ""
        self.controller.connect(addr, name)

    def _on_disconnect(self) -> None:
        self.controller.disconnect()

    def _on_ping(self) -> None:
        self.controller.send_frame(BLE_CMD_PING, b"")

    def _on_upload(self) -> None:
        if self._planes is None:
            self._log("no preview planes — pick a template or import image")
            return
        bw, red = self._planes
        mode = self._mode_from_ui()
        page = self._page_from_ui()
        label = f"{MODE_NAMES.get(mode, mode)}/{page}"
        self._refresh_slot_hint()
        self._log(
            f"upload start [{label}] (α→home/0) — "
            "上传完成后设备端刷屏；忙时 ACK=-6 请稍候再传"
        )
        self.controller.upload_planes(
            bw, red, label=label, width=PANEL_WIDTH, height=PANEL_HEIGHT
        )

    def _on_designer(self) -> None:
        sync_back(self.ink_cfg, self.app_cfg)
        bridge = write_nrf_console_bridge(self.ink_cfg)
        nrf_root = nrf_console_root()
        main_py = nrf_root / "main.py"
        if not main_py.is_file():
            self._log(f"nrf_console missing: {main_py}")
            return
        env = os.environ.copy()
        env["NRF_CONSOLE_CONFIG"] = str(bridge)
        self._log(f"launch designer bridge={bridge}")
        subprocess.Popen(
            [sys.executable, str(main_py)],
            cwd=str(nrf_root),
            env=env,
        )

    def _address_from_label(self, label: str) -> str:
        if not label or label.startswith("("):
            return ""
        # "name  AA:BB:…"
        parts = label.rsplit("  ", 1)
        if len(parts) == 2 and ":" in parts[1]:
            return parts[1].strip()
        for d in self._devices:
            if d.address in label:
                return d.address
        return self.app_cfg.last_address

    def _refresh_controls(self) -> None:
        busy = self._op != "idle"
        self.btn_scan.configure(state="disabled" if busy else "normal")
        self.btn_conn.configure(state="disabled" if busy or self._connected else "normal")
        self.btn_disc.configure(state="normal" if self._connected and not busy else "disabled")
        self.btn_ping.configure(state="normal" if self._connected and not busy else "disabled")

    def _on_event(self, ev: UiEvent) -> None:
        if ev.kind == "log" and ev.message:
            self._log(ev.message)
        elif ev.kind == "error" and ev.message:
            self._log(f"ERR {ev.message}")
        elif ev.kind == "tx" and ev.message:
            self._log(ev.message)
        elif ev.kind == "rx" and ev.message:
            self._log(ev.message)
        elif ev.kind == "status" and ev.message:
            self.status.configure(text=ev.message)
        elif ev.kind == "busy" and isinstance(ev.data, dict):
            self._op = str(ev.data.get("op", "idle"))
            self._connected = bool(ev.data.get("connected"))
            self._refresh_controls()
        elif ev.kind == "connected":
            self._connected = bool(ev.data)
            self._refresh_controls()
            self.status.configure(text="connected" if self._connected else "idle")
            sync_back(self.ink_cfg, self.app_cfg)
            if self._connected:
                try:
                    save_config(self.ink_cfg)
                except Exception:  # noqa: BLE001
                    pass
                self._log("tip: 板端 BTN1 断连后，等 1s 再 Scan→Connect")
        elif ev.kind == "devices" and ev.data is not None:
            self._devices = list(ev.data)
            labels = [f"{d.name}  {d.address}" for d in self._devices]
            if not labels:
                labels = ["(no devices)"]
            self.device_menu.configure(values=labels)
            self.device_menu.set(labels[0])

    def _pump(self) -> None:
        self.controller.poll_events(self._on_event)
        self.after(80, self._pump)

    def _on_close(self) -> None:
        sync_back(self.ink_cfg, self.app_cfg)
        try:
            save_config(self.ink_cfg)
        except Exception:  # noqa: BLE001
            pass
        self.controller.stop()
        self.destroy()


def run_app(cfg: InkPanelConfig | None = None) -> None:
    app = ConsoleApp(cfg or load_config())
    app.mainloop()
