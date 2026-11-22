"""400×300 BWR preview canvas."""

from __future__ import annotations

import tempfile
from pathlib import Path
from tkinter import Canvas, PhotoImage

import customtkinter as ctk

from ink_panel_console.panel import PANEL
from ink_panel_console.preview_rgb import planes_to_rgb

# Fit ~ half of a 1280 window; 400*2=800 wide is large — use scale 1 or 2
DEFAULT_SCALE = 1


class PreviewPanel(ctk.CTkFrame):
    def __init__(
        self,
        master: ctk.CTk | ctk.CTkFrame,
        *,
        scale: int = DEFAULT_SCALE,
        **kwargs: object,
    ) -> None:
        super().__init__(master, corner_radius=12, fg_color="#2b2a27", **kwargs)  # type: ignore[arg-type]
        self._scale = max(1, scale)
        self._photo: PhotoImage | None = None

        ctk.CTkLabel(
            self,
            text=f"Preview · {PANEL.width}×{PANEL.height}",
            font=ctk.CTkFont(size=14, weight="bold"),
            text_color="#e8e2d4",
        ).pack(anchor="w", padx=12, pady=(10, 4))

        wrap = ctk.CTkFrame(self, corner_radius=8, fg_color="#1a1a18")
        wrap.pack(padx=12, pady=(0, 12))
        pw = PANEL.width * self._scale
        ph = PANEL.height * self._scale
        self.canvas = Canvas(
            wrap,
            width=pw,
            height=ph,
            highlightthickness=2,
            highlightbackground="#c23b2a",
            bg="#e8e2d4",
        )
        self.canvas.pack(padx=8, pady=8)

    def show_planes(self, bw: bytes, red: bytes, *, master: ctk.CTk) -> None:
        rgb = planes_to_rgb(bw, red, scale=self._scale)
        w = PANEL.width * self._scale
        h = PANEL.height * self._scale
        ppm = f"P6\n{w} {h}\n255\n".encode("ascii") + rgb
        path = Path(tempfile.gettempdir()) / "ink_panel_epd_preview.ppm"
        path.write_bytes(ppm)
        photo = PhotoImage(master=master, file=str(path))
        self._photo = photo
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=photo)
