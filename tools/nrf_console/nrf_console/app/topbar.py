"""Connection top bar for EPD Tag Designer."""

from __future__ import annotations

from collections.abc import Callable

import customtkinter as ctk

from nrf_console.transport import DeviceInfo


class TopBar(ctk.CTkFrame):
    def __init__(
        self,
        master: ctk.CTk,
        *,
        filter_text: str,
        on_scan: Callable[[], None],
        on_connect: Callable[[], None],
        on_disconnect: Callable[[], None],
        on_advanced: Callable[[], None],
        **kwargs: object,
    ) -> None:
        super().__init__(master, corner_radius=12, **kwargs)  # type: ignore[arg-type]
        self.grid_columnconfigure(3, weight=1)

        ctk.CTkLabel(self, text="Device", font=ctk.CTkFont(size=15, weight="bold")).grid(
            row=0, column=0, columnspan=7, sticky="w", padx=12, pady=(10, 4)
        )

        ctk.CTkLabel(self, text="Filter").grid(row=1, column=0, padx=(12, 6), pady=6, sticky="w")
        self.filter_entry = ctk.CTkEntry(self, width=120)
        self.filter_entry.insert(0, filter_text)
        self.filter_entry.grid(row=1, column=1, padx=4, pady=6, sticky="w")

        self.scan_btn = ctk.CTkButton(self, text="Scan", width=80, command=on_scan)
        self.scan_btn.grid(row=1, column=2, padx=4, pady=6)

        self.device_menu = ctk.CTkOptionMenu(self, values=["(no devices)"], width=260)
        self.device_menu.grid(row=1, column=3, padx=4, pady=6, sticky="ew")

        self.connect_btn = ctk.CTkButton(self, text="Connect", width=90, command=on_connect)
        self.connect_btn.grid(row=1, column=4, padx=4, pady=6)

        self.disconnect_btn = ctk.CTkButton(
            self, text="Disconnect", width=100, command=on_disconnect, state="disabled"
        )
        self.disconnect_btn.grid(row=1, column=5, padx=4, pady=6)

        self.advanced_btn = ctk.CTkButton(self, text="Advanced", width=90, command=on_advanced)
        self.advanced_btn.grid(row=1, column=6, padx=(4, 12), pady=6)

        self.status_label = ctk.CTkLabel(self, text="status: idle", anchor="w")
        self.status_label.grid(row=2, column=0, columnspan=7, sticky="ew", padx=12, pady=(0, 10))

    def set_status(self, text: str) -> None:
        self.status_label.configure(text=f"status: {text}")

    def set_devices(self, devices: list[DeviceInfo], labels: list[str], pick: str) -> None:
        if not devices:
            self.device_menu.configure(values=["(no devices)"])
            self.device_menu.set("(no devices)")
        else:
            self.device_menu.configure(values=labels)
            self.device_menu.set(pick)

    def refresh_states(self, *, idle: bool, connected: bool, has_devices: bool) -> None:
        def st(ok: bool) -> str:
            return "normal" if ok else "disabled"

        self.scan_btn.configure(state=st(idle))
        self.connect_btn.configure(state=st(idle and not connected and has_devices))
        self.disconnect_btn.configure(state=st(idle and connected))
        self.device_menu.configure(state=st(idle and not connected))
        self.filter_entry.configure(state=st(idle))
        self.advanced_btn.configure(state=st(True))
