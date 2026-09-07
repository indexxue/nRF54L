"""Protocol debug dialog (PING / DISPLAY / raw frames)."""

from __future__ import annotations

from collections.abc import Callable
from tkinter import messagebox

import customtkinter as ctk

from nrf_console.commands import CommandRegistry
from nrf_console.protocol.codec import encode_frame, format_hex, parse_hex


class AdvancedDialog(ctk.CTkToplevel):
    def __init__(
        self,
        master: ctk.CTk,
        *,
        registry: CommandRegistry,
        on_send_raw: Callable[[bytes], None],
        can_send: Callable[[], bool],
    ) -> None:
        super().__init__(master)
        self.title("Advanced — BLE Commands")
        self.geometry("520x280")
        self.resizable(False, False)
        self.transient(master)
        self.grab_set()

        self._registry = registry
        self._on_send_raw = on_send_raw
        self._can_send = can_send
        self._cmd_buttons: list[ctk.CTkButton] = []

        ctk.CTkLabel(self, text="Quick commands", font=ctk.CTkFont(size=14, weight="bold")).pack(
            anchor="w", padx=14, pady=(12, 4)
        )
        row = ctk.CTkFrame(self, fg_color="transparent")
        row.pack(anchor="w", padx=14, pady=4)
        for cdef in registry.list():
            btn = ctk.CTkButton(
                row,
                text=cdef.title,
                width=100,
                command=lambda k=cdef.key: self._send_registered(k),
            )
            btn.pack(side="left", padx=(0, 8))
            self._cmd_buttons.append(btn)

        form = ctk.CTkFrame(self, fg_color="transparent")
        form.pack(fill="x", padx=14, pady=8)
        form.grid_columnconfigure(3, weight=1)

        ctk.CTkLabel(form, text="cmd_id").grid(row=0, column=0, padx=(0, 6), pady=4, sticky="w")
        self.cmd_id_entry = ctk.CTkEntry(form, width=90)
        self.cmd_id_entry.insert(0, "1001")
        self.cmd_id_entry.grid(row=0, column=1, padx=4, pady=4)

        ctk.CTkLabel(form, text="payload hex").grid(row=0, column=2, padx=(12, 6), pady=4)
        self.payload_entry = ctk.CTkEntry(form)
        self.payload_entry.grid(row=0, column=3, padx=4, pady=4, sticky="ew")

        ctk.CTkLabel(form, text="raw frame").grid(row=1, column=0, padx=(0, 6), pady=4, sticky="w")
        self.raw_entry = ctk.CTkEntry(form)
        self.raw_entry.insert(0, "E9 03 00 00")
        self.raw_entry.grid(row=1, column=1, columnspan=2, padx=4, pady=4, sticky="ew")

        send_row = ctk.CTkFrame(form, fg_color="transparent")
        send_row.grid(row=1, column=3, padx=4, pady=4, sticky="e")
        self.send_cmd_btn = ctk.CTkButton(send_row, text="Send CMD", width=90, command=self._send_cmd)
        self.send_cmd_btn.pack(side="left", padx=(0, 6))
        self.send_raw_btn = ctk.CTkButton(send_row, text="Send Raw", width=90, command=self._send_raw)
        self.send_raw_btn.pack(side="left")

        self.after(50, self._refresh)
        self.protocol("WM_DELETE_WINDOW", self.destroy)

    def _guard(self) -> bool:
        if not self._can_send():
            messagebox.showinfo("EPD Tag Designer", "Connect first and wait until idle.", parent=self)
            return False
        return True

    def _refresh(self) -> None:
        ok = self._can_send()
        st = "normal" if ok else "disabled"
        for b in self._cmd_buttons:
            b.configure(state=st)
        self.send_cmd_btn.configure(state=st)
        self.send_raw_btn.configure(state=st)
        self.cmd_id_entry.configure(state=st)
        self.payload_entry.configure(state=st)
        self.raw_entry.configure(state=st)
        if self.winfo_exists():
            self.after(200, self._refresh)

    def _send_registered(self, key: str) -> None:
        if not self._guard():
            return
        self._on_send_raw(self._registry.get(key).build_frame())

    def _send_cmd(self) -> None:
        if not self._guard():
            return
        try:
            cmd_id = int(self.cmd_id_entry.get().strip())
            payload = parse_hex(self.payload_entry.get())
            data = encode_frame(cmd_id, payload)
        except ValueError as exc:
            messagebox.showerror("EPD Tag Designer", str(exc), parent=self)
            return
        self.raw_entry.configure(state="normal")
        self.raw_entry.delete(0, "end")
        self.raw_entry.insert(0, format_hex(data))
        self._on_send_raw(data)

    def _send_raw(self) -> None:
        if not self._guard():
            return
        try:
            data = parse_hex(self.raw_entry.get())
        except ValueError as exc:
            messagebox.showerror("EPD Tag Designer", str(exc), parent=self)
            return
        if not data:
            messagebox.showinfo("EPD Tag Designer", "Empty frame.", parent=self)
            return
        self._on_send_raw(data)
