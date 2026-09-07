"""Collapsible activity log."""

from __future__ import annotations

from collections.abc import Callable

import customtkinter as ctk

_COLLAPSED_H = 90
_EXPANDED_H = 220


class LogPanel(ctk.CTkFrame):
    def __init__(
        self,
        master: ctk.CTk,
        *,
        on_clear: Callable[[], None] | None = None,
        on_save_cfg: Callable[[], None] | None = None,
        **kwargs: object,
    ) -> None:
        super().__init__(master, corner_radius=12, **kwargs)  # type: ignore[arg-type]
        self._expanded = False
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        header = ctk.CTkFrame(self, fg_color="transparent")
        header.grid(row=0, column=0, sticky="ew", padx=12, pady=(10, 4))
        ctk.CTkLabel(header, text="Log", font=ctk.CTkFont(size=15, weight="bold")).pack(side="left")
        self.toggle_btn = ctk.CTkButton(header, text="Expand", width=80, command=self._toggle)
        self.toggle_btn.pack(side="right")
        if on_clear:
            ctk.CTkButton(header, text="Clear", width=70, command=on_clear).pack(
                side="right", padx=(0, 8)
            )
        if on_save_cfg:
            ctk.CTkButton(header, text="Save Config", width=110, command=on_save_cfg).pack(
                side="right", padx=(0, 8)
            )

        self.log_box = ctk.CTkTextbox(
            self, height=_COLLAPSED_H, font=ctk.CTkFont(family="Consolas", size=12)
        )
        self.log_box.grid(row=1, column=0, sticky="nsew", padx=12, pady=(0, 12))
        self.log_box.configure(state="disabled")

    def _toggle(self) -> None:
        self._expanded = not self._expanded
        h = _EXPANDED_H if self._expanded else _COLLAPSED_H
        self.toggle_btn.configure(text="Collapse" if self._expanded else "Expand")
        self.log_box.configure(height=h)

    def append(self, line: str) -> None:
        self.log_box.configure(state="normal")
        self.log_box.insert("end", line + "\n")
        self.log_box.see("end")
        self.log_box.configure(state="disabled")

    def clear(self) -> None:
        self.log_box.configure(state="normal")
        self.log_box.delete("1.0", "end")
        self.log_box.configure(state="disabled")
