"""EPD plane preview (PPM via temp file for Windows Tk)."""

from __future__ import annotations

import tempfile
from pathlib import Path
from tkinter import Canvas, PhotoImage

import customtkinter as ctk

from nrf_console.epd.display import Canvas as EpdCanvas
from nrf_console.epd.plane import HEIGHT, WIDTH

DEFAULT_SCALE = 2


class PreviewPanel(ctk.CTkFrame):
    def __init__(self, master: ctk.CTk | ctk.CTkFrame, *, scale: int = DEFAULT_SCALE, **kwargs: object) -> None:
        super().__init__(master, corner_radius=12, **kwargs)  # type: ignore[arg-type]
        self._scale = max(1, scale)
        self._photo: PhotoImage | None = None

        ctk.CTkLabel(self, text="Preview", font=ctk.CTkFont(size=15, weight="bold")).pack(
            anchor="w", padx=12, pady=(10, 4)
        )

        wrap = ctk.CTkFrame(self, corner_radius=8)
        wrap.pack(padx=12, pady=(0, 12))
        pw = WIDTH * self._scale
        ph = HEIGHT * self._scale
        self.canvas = Canvas(
            wrap,
            width=pw,
            height=ph,
            highlightthickness=1,
            highlightbackground="#666666",
            bg="#F5F5F0",
        )
        self.canvas.pack(padx=6, pady=6)

    def show_planes(self, bw: bytes, red: bytes, *, master: ctk.CTk) -> None:
        c = EpdCanvas()
        c.bw[:] = bw
        c.red[:] = red
        rgb = c.preview_rgb(scale=self._scale)
        w = WIDTH * self._scale
        h = HEIGHT * self._scale
        ppm = f"P6\n{w} {h}\n255\n".encode("ascii") + rgb
        path = Path(tempfile.gettempdir()) / "nrf_console_epd_preview.ppm"
        path.write_bytes(ppm)
        photo = PhotoImage(master=master, file=str(path))
        self._photo = photo
        self.canvas.delete("all")
        self.canvas.create_image(0, 0, anchor="nw", image=photo)
