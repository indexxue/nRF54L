"""Template / image editor panel."""

from __future__ import annotations

from collections.abc import Callable
from pathlib import Path
from tkinter import filedialog

import customtkinter as ctk

from nrf_console.epd.pages import FIELD_LAYOUTS, PAGE_CATALOG


class EditorPanel(ctk.CTkFrame):
    def __init__(
        self,
        master: ctk.CTk | ctk.CTkFrame,
        *,
        on_change: Callable[[], None],
        on_upload: Callable[[], None],
        **kwargs: object,
    ) -> None:
        super().__init__(master, corner_radius=12, **kwargs)  # type: ignore[arg-type]
        self._on_change = on_change
        self._image_path: Path | None = None
        self._mode = "template"

        ctk.CTkLabel(self, text="Design", font=ctk.CTkFont(size=15, weight="bold")).grid(
            row=0, column=0, columnspan=2, sticky="w", padx=12, pady=(10, 6)
        )

        self.mode_seg = ctk.CTkSegmentedButton(
            self,
            values=["Template", "Image"],
            command=self._on_mode,
        )
        self.mode_seg.set("Template")
        self.mode_seg.grid(row=1, column=0, columnspan=2, sticky="ew", padx=12, pady=4)

        # --- Template block ---
        self.tpl = ctk.CTkFrame(self, fg_color="transparent")
        self.tpl.grid(row=2, column=0, columnspan=2, sticky="ew", padx=8, pady=4)
        self.tpl.grid_columnconfigure(1, weight=1)

        ctk.CTkLabel(self.tpl, text="Layout").grid(row=0, column=0, padx=4, pady=4, sticky="w")
        self._label_to_key = {f"{k} — {title}": k for k, (title, _) in PAGE_CATALOG.items()}
        labels = list(self._label_to_key.keys())
        self.layout_menu = ctk.CTkOptionMenu(
            self.tpl, values=labels, width=240, command=lambda _v: self._fire_change()
        )
        # Prefer tag
        default = next((lab for lab, k in self._label_to_key.items() if k == "tag"), labels[0])
        self.layout_menu.set(default)
        self.layout_menu.grid(row=0, column=1, padx=4, pady=4, sticky="ew")

        self.title_entry = self._field(self.tpl, 1, "Title", "eink_tag")
        self.price_entry = self._field(self.tpl, 2, "Price", "12.80")
        self.sku_entry = self._field(self.tpl, 3, "SKU", "SKU-029Z95")

        # --- Image block ---
        self.img = ctk.CTkFrame(self, fg_color="transparent")
        self.img.grid(row=3, column=0, columnspan=2, sticky="ew", padx=8, pady=4)
        self.img.grid_columnconfigure(1, weight=1)

        self.img_path_label = ctk.CTkLabel(
            self.img, text="(no image)", text_color=("gray40", "gray65"), anchor="w"
        )
        self.img_path_label.grid(row=0, column=0, columnspan=2, sticky="ew", padx=4, pady=4)

        self.browse_btn = ctk.CTkButton(self.img, text="Browse…", width=100, command=self._browse)
        self.browse_btn.grid(row=1, column=0, padx=4, pady=4, sticky="w")

        ctk.CTkLabel(self.img, text="Luma thr").grid(row=2, column=0, padx=4, pady=4, sticky="w")
        self.luma_slider = ctk.CTkSlider(
            self.img, from_=40, to=220, number_of_steps=180, command=lambda _v: self._fire_change()
        )
        self.luma_slider.set(140)
        self.luma_slider.grid(row=2, column=1, padx=4, pady=4, sticky="ew")

        self.upload_btn = ctk.CTkButton(
            self, text="Upload to Tag", height=36, command=on_upload
        )
        self.upload_btn.grid(row=4, column=0, columnspan=2, sticky="ew", padx=12, pady=(12, 14))

        self._show_mode_widgets()

    def _field(self, parent: ctk.CTkFrame, row: int, label: str, default: str) -> ctk.CTkEntry:
        ctk.CTkLabel(parent, text=label).grid(row=row, column=0, padx=4, pady=4, sticky="w")
        entry = ctk.CTkEntry(parent)
        entry.insert(0, default)
        entry.grid(row=row, column=1, padx=4, pady=4, sticky="ew")
        entry.bind("<KeyRelease>", lambda _e: self._fire_change())
        return entry

    def _on_mode(self, value: str) -> None:
        self._mode = "image" if value == "Image" else "template"
        self._show_mode_widgets()
        self._fire_change()

    def _show_mode_widgets(self) -> None:
        if self._mode == "template":
            self.tpl.grid()
            self.img.grid_remove()
            self._sync_field_enable()
        else:
            self.tpl.grid_remove()
            self.img.grid()

    def _sync_field_enable(self) -> None:
        key = self.layout_key()
        editable = key in FIELD_LAYOUTS
        state = "normal" if editable else "disabled"
        for e in (self.title_entry, self.price_entry, self.sku_entry):
            e.configure(state=state)

    def _fire_change(self) -> None:
        if self._mode == "template":
            self._sync_field_enable()
        self._on_change()

    def _browse(self) -> None:
        path = filedialog.askopenfilename(
            title="Select image",
            filetypes=[
                ("Images", "*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"),
                ("All", "*.*"),
            ],
        )
        if not path:
            return
        self._image_path = Path(path)
        name = self._image_path.name
        if len(name) > 36:
            name = name[:33] + "..."
        self.img_path_label.configure(text=name)
        self._fire_change()

    def mode(self) -> str:
        return self._mode

    def layout_key(self) -> str:
        return self._label_to_key.get(self.layout_menu.get(), "tag")

    def tag_fields(self) -> dict[str, str]:
        return {
            "title": self.title_entry.get().strip(),
            "price": self.price_entry.get().strip(),
            "sku": self.sku_entry.get().strip(),
        }

    def image_path(self) -> Path | None:
        return self._image_path

    def luminance_threshold(self) -> int:
        return int(round(float(self.luma_slider.get())))

    def refresh_states(self, *, can_edit: bool, can_upload: bool) -> None:
        def st(ok: bool) -> str:
            return "normal" if ok else "disabled"

        self.mode_seg.configure(state=st(can_edit))
        self.layout_menu.configure(state=st(can_edit and self._mode == "template"))
        self.browse_btn.configure(state=st(can_edit and self._mode == "image"))
        self.luma_slider.configure(state=st(can_edit and self._mode == "image"))
        if can_edit and self._mode == "template":
            self._sync_field_enable()
        else:
            for e in (self.title_entry, self.price_entry, self.sku_entry):
                e.configure(state="disabled")
        self.upload_btn.configure(state=st(can_upload))
