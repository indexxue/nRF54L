"""ink_panel 桌牌助手 — 运营向导 UI（协议进高级）。"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from tkinter import filedialog

import customtkinter as ctk

from ink_panel_console.app import copy as C
from ink_panel_console.app.ble_bridge import (
    BLE_CMD_GET_SLOT_STATUS,
    BLE_CMD_PING,
    BLE_CMD_SET_GROUP_META,
    BleController,
    UiEvent,
    pack_set_group_meta,
    sync_back,
    to_app_config,
    utf8_fit,
)
from ink_panel_console.app.flow import (
    Step,
    Target,
    UiState,
    apply_slot_status,
    can_push,
    content_max,
    current_step,
    default_enable_count,
    describe_target,
    pages_for_mode,
    pick_strongest,
    push_block_reason,
    status_for_state,
)
from ink_panel_console.app.preview import PreviewPanel
from ink_panel_console.config import (
    GROUP_TITLE_MAX,
    MODE_HOME,
    MODE_INFO,
    MODE_NOTES,
    PANEL_HEIGHT,
    PANEL_WIDTH,
    InkPanelConfig,
    load_config,
    save_config,
    write_nrf_console_bridge,
)
from ink_panel_console.panel import image_to_planes
from ink_panel_console.paths import nrf_console_root
from ink_panel_console.templates.render import (
    NOTES_ITEMS_MAX,
    NOTES_ITEMS_MIN,
    InfoForm,
    NotesForm,
    NotesItem,
    compose_home_overlay,
    render_blank_home,
    render_info_door,
    render_notes_three,
    resolve_cjk_font,
)

_PAPER = "#e8e2d4"
_SHELL = "#2b2a27"
_RED = "#c23b2a"
_MUTED = "#9a9486"
_CARD = "#35332f"
_OK = "#2f6b3a"


class ConsoleApp(ctk.CTk):
    def __init__(self, cfg: InkPanelConfig) -> None:
        super().__init__()
        self.ink_cfg = cfg
        self.app_cfg = to_app_config(cfg)
        self.controller = BleController(self.app_cfg)
        self.ui = UiState()  # 勿用 self.state：会盖住 Tk window.state()
        self._devices: list = []
        self._one_click = False
        self._planes: tuple[bytes, bytes] | None = None  # 上传用（首页=干净底图）
        self._home_base: tuple[bytes, bytes] | None = None
        self._preview_src = "none"  # form | image | none
        self._notes_form = NotesForm()
        self._info_form = InfoForm()
        self._page_btns: dict[tuple[int, int], ctk.CTkButton] = {}
        self._render_job: str | None = None
        self._pending_select: tuple[int, int] | None = None
        self._pending_show: bool | None = None

        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("dark-blue")
        self.title(C.APP_TITLE)
        self.geometry(self.app_cfg.window_geometry)
        self.minsize(1180, 760)
        self.configure(fg_color=_SHELL)

        self._build()
        self.protocol("WM_DELETE_WINDOW", self._on_close)
        self.controller.start()
        self.after(80, self._pump)
        self._refresh_all()
        if resolve_cjk_font() is None:
            self._log("未找到系统中文字体，预览文字可能显示异常")

    # ── build ───────────────────────────────────────────

    def _build(self) -> None:
        self.grid_columnconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=1)

        top = ctk.CTkFrame(self, fg_color="#1f1e1c", corner_radius=0)
        top.grid(row=0, column=0, sticky="ew")
        top.grid_columnconfigure(2, weight=1)

        ctk.CTkLabel(
            top,
            text=C.APP_TITLE,
            font=ctk.CTkFont(family="Segoe UI", size=18, weight="bold"),
            text_color=_PAPER,
        ).grid(row=0, column=0, padx=(16, 8), pady=(10, 0))

        self.page_banner = ctk.CTkLabel(
            top,
            text="未连接 · 记事 — 页 · 信息 — 页",
            text_color=_MUTED,
            font=ctk.CTkFont(size=12),
            anchor="w",
        )
        self.page_banner.grid(row=1, column=0, columnspan=3, sticky="w", padx=(16, 8), pady=(0, 10))

        steps = ctk.CTkFrame(top, fg_color="transparent")
        steps.grid(row=0, column=1, rowspan=2, padx=8, pady=12)
        self._step_labels: dict[Step, ctk.CTkLabel] = {}
        for i, (step, text) in enumerate(
            (
                (Step.CONNECT, C.STEP_CONNECT),
                (Step.PAGE, C.STEP_PAGE),
                (Step.EDIT, C.STEP_EDIT),
                (Step.PUSH, C.STEP_PUSH),
            )
        ):
            lb = ctk.CTkLabel(steps, text=text, text_color=_MUTED, font=ctk.CTkFont(size=13))
            lb.pack(side="left", padx=6)
            self._step_labels[step] = lb

        self.btn_connect = ctk.CTkButton(
            top, text=C.BTN_CONNECT, width=110, fg_color=_RED, command=self._on_one_click
        )
        self.btn_connect.grid(row=0, column=3, rowspan=2, padx=6, pady=12)
        self.btn_disc = ctk.CTkButton(
            top, text=C.BTN_DISCONNECT, width=72, fg_color="#444", command=self._on_disconnect
        )
        self.btn_disc.grid(row=0, column=4, rowspan=2, padx=(4, 16), pady=12)

        main = ctk.CTkFrame(self, fg_color="transparent")
        main.grid(row=1, column=0, sticky="nsew", padx=12, pady=8)
        main.grid_columnconfigure(2, weight=1)
        main.grid_rowconfigure(0, weight=1)

        # left: pages
        left = ctk.CTkScrollableFrame(main, fg_color=_CARD, corner_radius=12, width=260)
        left.grid(row=0, column=0, sticky="ns", padx=(0, 8))
        ctk.CTkLabel(
            left, text="选择页面", font=ctk.CTkFont(size=14, weight="bold"), text_color=_PAPER
        ).pack(anchor="w", padx=12, pady=(12, 6))
        self.pages_host = ctk.CTkFrame(left, fg_color="transparent")
        self.pages_host.pack(fill="x", padx=8, pady=4)
        self._rebuild_page_list()

        # mid: editor
        mid = ctk.CTkFrame(main, fg_color=_CARD, corner_radius=12, width=300)
        mid.grid(row=0, column=1, sticky="ns", padx=(0, 8))
        mid.grid_propagate(False)
        ctk.CTkLabel(
            mid, text="编辑内容", font=ctk.CTkFont(size=14, weight="bold"), text_color=_PAPER
        ).pack(anchor="w", padx=12, pady=(12, 4))
        self.editor_hint = ctk.CTkLabel(mid, text="先连接并选择页面", text_color=_MUTED)
        self.editor_hint.pack(anchor="w", padx=12, pady=(0, 8))

        self.editor_stack = ctk.CTkFrame(mid, fg_color="transparent")
        self.editor_stack.pack(fill="both", expand=True, padx=8, pady=4)
        self._build_notes_editor(self.editor_stack)
        self._build_info_editor(self.editor_stack)
        self._build_home_editor(self.editor_stack)

        ctk.CTkButton(
            mid, text=C.BTN_IMPORT, fg_color="#5a4038", command=self._on_import_image
        ).pack(fill="x", padx=12, pady=(8, 14))

        # right: preview + push
        right = ctk.CTkFrame(main, fg_color="transparent")
        right.grid(row=0, column=2, sticky="nsew")
        right.grid_rowconfigure(0, weight=1)
        right.grid_columnconfigure(0, weight=1)

        self.preview = PreviewPanel(right, scale=1)
        self.preview.grid(row=0, column=0, sticky="nsew")

        actions = ctk.CTkFrame(right, fg_color=_CARD, corner_radius=12)
        actions.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        self.guide = ctk.CTkLabel(
            actions, text="", text_color=_MUTED, wraplength=420, justify="left"
        )
        self.guide.pack(anchor="w", padx=14, pady=(10, 4))
        row = ctk.CTkFrame(actions, fg_color="transparent")
        row.pack(fill="x", padx=12, pady=(4, 12))
        self.btn_push = ctk.CTkButton(
            row,
            text=C.BTN_PUSH_SHOW,
            height=40,
            font=ctk.CTkFont(size=15, weight="bold"),
            fg_color=_RED,
            command=lambda: self._on_push(show=True),
        )
        self.btn_push.pack(side="left", padx=(0, 8))
        self.btn_save = ctk.CTkButton(
            row, text=C.BTN_PUSH_SAVE, height=40, fg_color="#4a4740", command=lambda: self._on_push(show=False)
        )
        self.btn_save.pack(side="left")

        # bottom
        bottom = ctk.CTkFrame(self, fg_color="#1f1e1c", corner_radius=0)
        bottom.grid(row=2, column=0, sticky="ew")
        bottom.grid_columnconfigure(0, weight=1)
        self.status = ctk.CTkLabel(bottom, text=C.format_status("idle"), text_color=_MUTED, anchor="w")
        self.status.grid(row=0, column=0, sticky="ew", padx=16, pady=(8, 0))

        self.adv_open = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(
            bottom,
            text=C.BTN_ADVANCED,
            variable=self.adv_open,
            command=self._toggle_advanced,
            text_color=_MUTED,
        ).grid(row=0, column=1, padx=12, pady=(8, 0))

        self.adv_frame = ctk.CTkFrame(bottom, fg_color="#151413", corner_radius=0)
        self.adv_frame.grid(row=1, column=0, columnspan=2, sticky="ew", padx=8, pady=8)
        self.adv_frame.grid_remove()
        self._build_advanced(self.adv_frame)

    def _build_notes_editor(self, parent: ctk.CTkFrame) -> None:
        self.notes_frame = ctk.CTkFrame(parent, fg_color="transparent")
        ctk.CTkLabel(
            self.notes_frame,
            text="记事（1～5 条自适应）",
            text_color=_PAPER,
            font=ctk.CTkFont(size=13, weight="bold"),
        ).pack(anchor="w")
        self.notes_title = ctk.CTkEntry(self.notes_frame, placeholder_text="自定义标题，如 今日事项")
        self.notes_title.insert(0, self._notes_form.title)
        self.notes_title.pack(fill="x", pady=4)
        self.notes_title.bind("<KeyRelease>", lambda _e: self._schedule_render())

        self.notes_items_host = ctk.CTkScrollableFrame(
            self.notes_frame, fg_color="transparent", height=280
        )
        self.notes_items_host.pack(fill="both", expand=True, pady=4)
        self._notes_rows: list[dict] = []

        bar = ctk.CTkFrame(self.notes_frame, fg_color="transparent")
        bar.pack(fill="x", pady=4)
        ctk.CTkButton(bar, text="＋ 添加", width=72, height=28, command=self._notes_add_item).pack(
            side="left", padx=(0, 4)
        )
        ctk.CTkButton(
            bar, text="－ 删除末条", width=88, height=28, fg_color="#5a4038", command=self._notes_del_item
        ).pack(side="left")

        self.notes_accent = ctk.CTkOptionMenu(
            self.notes_frame,
            values=["无强调"],
            command=lambda _v: self._schedule_render(),
        )
        self.notes_accent.set("无强调")
        self.notes_accent.pack(fill="x", pady=(6, 2))
        ctk.CTkLabel(
            self.notes_frame,
            text="每条可勾完成、填时间；红强调可选一条",
            text_color=_MUTED,
            font=ctk.CTkFont(size=11),
        ).pack(anchor="w")

        # 默认 3 条
        for _ in range(3):
            self._notes_add_item(render=False)
        self._notes_refresh_accent_menu()


    def _build_info_editor(self, parent: ctk.CTkFrame) -> None:
        self.info_frame = ctk.CTkFrame(parent, fg_color="transparent")
        ctk.CTkLabel(self.info_frame, text="门牌状态", text_color=_PAPER).pack(anchor="w")
        self.info_title = ctk.CTkEntry(self.info_frame, placeholder_text="标题")
        self.info_title.insert(0, self._info_form.title)
        self.info_title.pack(fill="x", pady=4)
        self.info_title.bind("<KeyRelease>", lambda _e: self._schedule_render())
        self.info_status = ctk.CTkEntry(self.info_frame, placeholder_text="状态一句，如 空闲 / 会议中")
        self.info_status.insert(0, self._info_form.status)
        self.info_status.pack(fill="x", pady=3)
        self.info_status.bind("<KeyRelease>", lambda _e: self._schedule_render())
        self.info_red = ctk.BooleanVar(value=True)
        ctk.CTkCheckBox(
            self.info_frame,
            text="状态用红色强调",
            variable=self.info_red,
            command=self._schedule_render,
            text_color=_PAPER,
        ).pack(anchor="w", pady=4)
        self.info_note = ctk.CTkEntry(self.info_frame, placeholder_text="备注（可选）")
        self.info_note.pack(fill="x", pady=3)
        self.info_note.bind("<KeyRelease>", lambda _e: self._schedule_render())

    def _build_home_editor(self, parent: ctk.CTkFrame) -> None:
        self.home_frame = ctk.CTkFrame(parent, fg_color="transparent")
        ctk.CTkLabel(
            self.home_frame,
            text="首页底图写一次即可。\n叠层数据由桌牌自己填（页数/电量）。",
            text_color=_MUTED,
            justify="left",
        ).pack(anchor="w", pady=8)
        ctk.CTkButton(
            self.home_frame, text="使用占位底图", fg_color="#4a4740", command=self._use_home_placeholder
        ).pack(fill="x", pady=4)
        ctk.CTkLabel(
            self.home_frame, text="首页叠层（写入设备后由 MCU 显示）", text_color=_PAPER
        ).pack(anchor="w", pady=(12, 4))
        self.overlay_pages = ctk.BooleanVar(value=True)
        self.overlay_batt = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(
            self.home_frame,
            text="显示页数信息（墨水屏底部条 · MCU 填数）",
            variable=self.overlay_pages,
            command=self._refresh_home_preview,
            text_color=_PAPER,
        ).pack(anchor="w", pady=2)
        ctk.CTkCheckBox(
            self.home_frame,
            text="显示电量（墨水屏底部 · MCU 占位 --）",
            variable=self.overlay_batt,
            command=self._refresh_home_preview,
            text_color=_PAPER,
        ).pack(anchor="w", pady=2)
        ctk.CTkButton(
            self.home_frame,
            text="保存叠层开关到桌牌",
            fg_color=_OK,
            command=self._on_save_overlays,
        ).pack(fill="x", pady=(8, 4))
        ctk.CTkLabel(
            self.home_frame,
            text="电量：由桌牌填写（工具不填数值）",
            text_color=_MUTED,
            font=ctk.CTkFont(size=11),
        ).pack(anchor="w", pady=2)

    def _build_advanced(self, parent: ctk.CTkFrame) -> None:
        parent.grid_columnconfigure(1, weight=1)
        bar = ctk.CTkFrame(parent, fg_color="transparent")
        bar.grid(row=0, column=0, columnspan=2, sticky="ew", padx=8, pady=6)
        self.filter_entry = ctk.CTkEntry(bar, width=120, placeholder_text="filter")
        self.filter_entry.insert(0, self.ink_cfg.device_name_filter)
        self.filter_entry.pack(side="left", padx=4)
        ctk.CTkButton(bar, text="扫描", width=64, command=self._on_manual_scan).pack(side="left", padx=2)
        self.device_menu = ctk.CTkOptionMenu(bar, values=["(先扫描)"], width=220)
        self.device_menu.pack(side="left", padx=4)
        ctk.CTkButton(bar, text="连接所选", width=88, command=self._on_manual_connect).pack(
            side="left", padx=2
        )
        ctk.CTkButton(bar, text="PING", width=56, command=self._on_ping).pack(side="left", padx=2)
        ctk.CTkButton(bar, text="拉槽状态", width=80, command=self._on_get_status).pack(
            side="left", padx=2
        )
        ctk.CTkButton(bar, text="DIY 设计器", width=96, command=self._on_designer).pack(
            side="left", padx=2
        )

        opts = ctk.CTkFrame(parent, fg_color="transparent")
        opts.grid(row=1, column=0, columnspan=2, sticky="ew", padx=8)
        self.allow_unconf = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(
            opts,
            text="允许推到未启用页（自动加页数）",
            variable=self.allow_unconf,
            command=self._on_allow_unconf,
            text_color=_MUTED,
        ).pack(side="left", padx=4)

        meta = ctk.CTkFrame(parent, fg_color="transparent")
        meta.grid(row=2, column=0, columnspan=2, sticky="ew", padx=8, pady=4)
        self.adv_notes_title = ctk.CTkEntry(meta, width=140, placeholder_text="记事标题")
        self.adv_notes_title.pack(side="left", padx=2)
        self.adv_notes_count = ctk.CTkEntry(meta, width=40)
        self.adv_notes_count.insert(0, "3")
        self.adv_notes_count.pack(side="left", padx=2)
        ctk.CTkButton(meta, text="写记事组", width=80, command=self._adv_set_notes).pack(
            side="left", padx=2
        )
        self.adv_info_title = ctk.CTkEntry(meta, width=140, placeholder_text="信息标题")
        self.adv_info_title.pack(side="left", padx=8)
        self.adv_info_count = ctk.CTkEntry(meta, width=40)
        self.adv_info_count.insert(0, "2")
        self.adv_info_count.pack(side="left", padx=2)
        ctk.CTkButton(meta, text="写信息组", width=80, command=self._adv_set_info).pack(
            side="left", padx=2
        )

        self.log = ctk.CTkTextbox(parent, height=100, fg_color="#0e0e0d", text_color=_PAPER)
        self.log.grid(row=3, column=0, columnspan=2, sticky="ew", padx=8, pady=(4, 8))

    # ── page list ───────────────────────────────────────

    def _rebuild_page_list(self) -> None:
        for w in self.pages_host.winfo_children():
            w.destroy()
        self._page_btns.clear()

        def section(title: str) -> ctk.CTkFrame:
            ctk.CTkLabel(self.pages_host, text=title, text_color=_PAPER, font=ctk.CTkFont(size=12, weight="bold")).pack(
                anchor="w", padx=4, pady=(8, 2)
            )
            fr = ctk.CTkFrame(self.pages_host, fg_color="transparent")
            fr.pack(fill="x", padx=2, pady=2)
            return fr

        # home
        fr = section(C.GROUP_HOME)
        self._add_page_btn(fr, MODE_HOME, 0, C.GROUP_HOME)

        # notes
        n_title = self.ui.notes_title or C.GROUP_NOTES
        fr = section(f"{C.GROUP_NOTES}「{n_title}」")
        pages = pages_for_mode(self.ui, MODE_NOTES)
        if not pages:
            ctk.CTkButton(
                fr,
                text=C.BTN_ENABLE_NOTES,
                fg_color=_OK,
                command=lambda: self._add_content_page(MODE_NOTES),
            ).pack(fill="x", pady=2)
        else:
            for p in pages:
                self._add_page_btn(fr, MODE_NOTES, p, f"第 {p} 页")
            row = ctk.CTkFrame(fr, fg_color="transparent")
            row.pack(fill="x", pady=(6, 2))
            ctk.CTkButton(
                row,
                text=C.BTN_ADD_PAGE,
                fg_color=_OK,
                height=28,
                command=lambda: self._add_content_page(MODE_NOTES),
            ).pack(side="left", fill="x", expand=True, padx=(0, 3))
            ctk.CTkButton(
                row,
                text=C.BTN_DEL_PAGE,
                fg_color="#5a4038",
                height=28,
                command=lambda: self._delete_last_page(MODE_NOTES),
            ).pack(side="left", fill="x", expand=True, padx=(3, 0))

        # info
        i_title = self.ui.info_title or C.GROUP_INFO
        fr = section(f"{C.GROUP_INFO}「{i_title}」")
        pages = pages_for_mode(self.ui, MODE_INFO)
        if not pages:
            ctk.CTkButton(
                fr,
                text=C.BTN_ENABLE_INFO,
                fg_color=_OK,
                command=lambda: self._add_content_page(MODE_INFO),
            ).pack(fill="x", pady=2)
        else:
            for p in pages:
                self._add_page_btn(fr, MODE_INFO, p, f"第 {p} 页")
            row = ctk.CTkFrame(fr, fg_color="transparent")
            row.pack(fill="x", pady=(6, 2))
            ctk.CTkButton(
                row,
                text=C.BTN_ADD_PAGE,
                fg_color=_OK,
                height=28,
                command=lambda: self._add_content_page(MODE_INFO),
            ).pack(side="left", fill="x", expand=True, padx=(0, 3))
            ctk.CTkButton(
                row,
                text=C.BTN_DEL_PAGE,
                fg_color="#5a4038",
                height=28,
                command=lambda: self._delete_last_page(MODE_INFO),
            ).pack(side="left", fill="x", expand=True, padx=(3, 0))

    def _add_page_btn(self, parent: ctk.CTkFrame, mode: int, page: int, label: str) -> None:
        btn = ctk.CTkButton(
            parent,
            text=label,
            anchor="w",
            fg_color="#4a4740",
            hover_color=_RED,
            command=lambda m=mode, p=page: self._select_target(m, p),
        )
        btn.pack(fill="x", pady=2)
        self._page_btns[(mode, page)] = btn

    def _select_target(self, mode: int, page: int) -> None:
        self.ui.target = Target(mode=mode, page=page)
        self._show_editor_for_mode(mode)
        if mode == MODE_HOME:
            if self._preview_src != "image":
                self._use_home_placeholder()
        else:
            self._schedule_render()
        self._refresh_all()

    def _show_editor_for_mode(self, mode: int) -> None:
        self.notes_frame.pack_forget()
        self.info_frame.pack_forget()
        self.home_frame.pack_forget()
        if mode == MODE_NOTES:
            self.notes_frame.pack(fill="both", expand=True)
            self.editor_hint.configure(text="填写事项，右侧实时预览")
        elif mode == MODE_INFO:
            self.info_frame.pack(fill="both", expand=True)
            self.editor_hint.configure(text="填写状态，右侧实时预览")
        else:
            self.home_frame.pack(fill="both", expand=True)
            self.editor_hint.configure(text="首页：导入 Logo 或占位图")

    # ── render ──────────────────────────────────────────

    def _schedule_render(self) -> None:
        if self._render_job is not None:
            try:
                self.after_cancel(self._render_job)
            except Exception:  # noqa: BLE001
                pass
        self._render_job = self.after(180, self._render_form)

    def _notes_add_item(self, *, render: bool = True) -> None:
        if len(self._notes_rows) >= NOTES_ITEMS_MAX:
            self._log(f"最多 {NOTES_ITEMS_MAX} 条")
            return
        idx = len(self._notes_rows)
        row = ctk.CTkFrame(self.notes_items_host, fg_color="#2a2826", corner_radius=6)
        row.pack(fill="x", pady=3)
        head = ctk.CTkFrame(row, fg_color="transparent")
        head.pack(fill="x", padx=6, pady=(4, 0))
        ctk.CTkLabel(head, text=f"#{idx + 1}", text_color=_MUTED, width=28).pack(side="left")
        done_var = ctk.BooleanVar(value=False)
        ctk.CTkCheckBox(
            head, text="完成", variable=done_var, width=60, command=self._schedule_render, text_color=_PAPER
        ).pack(side="left", padx=4)
        time_e = ctk.CTkEntry(head, width=64, placeholder_text="时间")
        time_e.pack(side="right", padx=2)
        time_e.bind("<KeyRelease>", lambda _e: self._schedule_render())
        text_e = ctk.CTkEntry(row, placeholder_text="事项内容")
        text_e.pack(fill="x", padx=6, pady=(2, 6))
        text_e.bind("<KeyRelease>", lambda _e: self._schedule_render())
        self._notes_rows.append(
            {"frame": row, "done": done_var, "time": time_e, "text": text_e}
        )
        self._notes_refresh_accent_menu()
        if render:
            self._schedule_render()

    def _notes_del_item(self) -> None:
        if len(self._notes_rows) <= NOTES_ITEMS_MIN:
            self._log(f"至少保留 {NOTES_ITEMS_MIN} 条")
            return
        row = self._notes_rows.pop()
        row["frame"].destroy()
        # 重标序号
        for i, r in enumerate(self._notes_rows):
            for child in r["frame"].winfo_children():
                if isinstance(child, ctk.CTkFrame):
                    for sub in child.winfo_children():
                        if isinstance(sub, ctk.CTkLabel) and str(sub.cget("text")).startswith("#"):
                            sub.configure(text=f"#{i + 1}")
                            break
        self._notes_refresh_accent_menu()
        self._schedule_render()

    def _notes_refresh_accent_menu(self) -> None:
        n = len(self._notes_rows)
        values = ["无强调"] + [f"强调第 {i} 条" for i in range(1, n + 1)]
        cur = self.notes_accent.get() if hasattr(self, "notes_accent") else "无强调"
        self.notes_accent.configure(values=values)
        if cur not in values:
            self.notes_accent.set("无强调")
        else:
            self.notes_accent.set(cur)

    def _read_notes_form(self) -> NotesForm:
        page = self.ui.target.page if self.ui.target else 1
        items: list[NotesItem] = []
        for r in self._notes_rows:
            items.append(
                NotesItem(
                    text=r["text"].get(),
                    done=bool(r["done"].get()),
                    time=r["time"].get().strip(),
                )
            )
        if not items:
            items = [NotesItem()]
        accent = 0
        label = self.notes_accent.get()
        if label.startswith("强调第"):
            try:
                accent = int(label.replace("强调第", "").replace("条", "").strip())
            except ValueError:
                accent = 0
        if accent > len(items):
            accent = 0
        return NotesForm(
            title=self.notes_title.get().strip() or "今日事项",
            items=items,
            accent=accent,
            page=page,
        )

    def _read_info_form(self) -> InfoForm:
        page = self.ui.target.page if self.ui.target else 1
        return InfoForm(
            title=self.info_title.get().strip() or "信息牌",
            status=self.info_status.get().strip() or "—",
            status_red=bool(self.info_red.get()),
            note=self.info_note.get().strip(),
            page=page,
        )

    def _render_form(self) -> None:
        self._render_job = None
        t = self.ui.target
        if t is None:
            return
        try:
            if t.mode == MODE_NOTES:
                bw, red = render_notes_three(self._read_notes_form())
            elif t.mode == MODE_INFO:
                bw, red = render_info_door(self._read_info_form())
            else:
                return
        except Exception as exc:  # noqa: BLE001
            self._log(f"渲染失败: {exc}")
            return
        self._planes = (bw, red)
        self._preview_src = "form"
        self.ui.has_preview = True
        self.preview.show_planes(bw, red, master=self)
        self._refresh_all()

    def _refresh_home_preview(self) -> None:
        """预览 = 底图 + 叠层；上传仍用干净底图。"""
        if self._home_base is None:
            return
        bw, red = self._home_base
        self._planes = (bw, red)
        prev_bw, prev_red = compose_home_overlay(
            bw,
            red,
            show_pages=bool(self.overlay_pages.get()),
            show_batt=bool(self.overlay_batt.get()),
            notes_count=self.ui.notes_count,
            info_count=self.ui.info_count,
        )
        self.ui.has_preview = True
        self.preview.show_planes(prev_bw, prev_red, master=self)
        self._refresh_all()

    def _use_home_placeholder(self) -> None:
        bw, red = render_blank_home()
        self._home_base = (bw, red)
        self._preview_src = "form"
        self._refresh_home_preview()

    def _on_import_image(self) -> None:
        path = filedialog.askopenfilename(
            title="导入图片 → 400×300",
            filetypes=[("Images", "*.png;*.jpg;*.jpeg;*.bmp;*.webp"), ("All", "*.*")],
        )
        if not path:
            return
        try:
            bw, red = image_to_planes(path)
        except Exception as exc:  # noqa: BLE001
            self._log(f"导入失败: {exc}")
            return
        t = self.ui.target
        if t is not None and t.mode == MODE_HOME:
            self._home_base = (bw, red)
            self._preview_src = "image"
            self._refresh_home_preview()
            self._log(f"已导入首页底图 {Path(path).name}（叠层仅预览/上屏）")
        else:
            self._planes = (bw, red)
            self._preview_src = "image"
            self.ui.has_preview = True
            self.preview.show_planes(bw, red, master=self)
            self._log(f"已导入 {Path(path).name}")
            self._refresh_all()

    # ── connect / push ──────────────────────────────────

    def _on_one_click(self) -> None:
        if self.ui.connected or self.ui.busy:
            return
        self._one_click = True
        self.ui.status_key = "scanning"
        self.app_cfg.device_name_filter = self.filter_entry.get().strip() or "ink_panel"
        self.ink_cfg.device_name_filter = self.app_cfg.device_name_filter
        self._refresh_all()
        self.controller.scan()

    def _on_disconnect(self) -> None:
        self.controller.disconnect()

    def _on_manual_scan(self) -> None:
        self._one_click = False
        self.ui.status_key = "scanning"
        self.app_cfg.device_name_filter = self.filter_entry.get().strip() or "ink_panel"
        self._refresh_all()
        self.controller.scan()

    def _on_manual_connect(self) -> None:
        label = self.device_menu.get()
        addr = self._address_from_label(label)
        if not addr:
            self._log("请先扫描并选择设备")
            return
        self.ui.status_key = "connecting"
        self._refresh_all()
        self.controller.connect(addr, label.split("  ")[0] if "  " in label else "")

    def _on_push(self, *, show: bool) -> None:
        reason = push_block_reason(self.ui)
        if reason:
            self.guide.configure(text=reason)
            return
        assert self.ui.target is not None and self._planes is not None
        bw, red = self._planes
        t = self.ui.target
        # 高级：推未启用页时先抬 count
        if self.ui.allow_unconfigured and t.mode in (MODE_NOTES, MODE_INFO):
            cur = self.ui.notes_count if t.mode == MODE_NOTES else self.ui.info_count
            if t.page > cur:
                title = self.ui.notes_title if t.mode == MODE_NOTES else self.ui.info_title
                try:
                    payload = pack_set_group_meta(t.mode, t.page, title or "")
                    self.controller.send_frame(BLE_CMD_SET_GROUP_META, payload)
                except ValueError as exc:
                    self._log(str(exc))
                    return
        label = describe_target(t)
        self.ui.status_key = "pushing"
        self.ui.busy = True
        self._refresh_all()
        self.ink_cfg.begin_with_slot = True
        self.controller.upload_planes(
            bw,
            red,
            label=label,
            width=PANEL_WIDTH,
            height=PANEL_HEIGHT,
            mode=t.mode,
            page_id=t.page,
            show=show,
        )
        self._pending_show = show

    def _add_content_page(self, mode: int) -> None:
        """左侧：添加记事/信息的一页（count+1，上限 notes7 / info3）。"""
        if not self.ui.connected:
            self.guide.configure(text=C.format_status("hint_disabled_connect"))
            return
        cur = self.ui.notes_count if mode == MODE_NOTES else self.ui.info_count
        max_c = content_max(mode)
        name = "记事" if mode == MODE_NOTES else "信息"
        if cur >= max_c:
            self._log(f"{name}已满（最多 {max_c} 页）")
            return
        new_count = cur + 1 if cur > 0 else default_enable_count(mode)
        title = self.ui.notes_title if mode == MODE_NOTES else self.ui.info_title
        if not title:
            title = name
        try:
            payload = pack_set_group_meta(mode, new_count, title)
        except ValueError as exc:
            self._log(str(exc))
            return
        self.controller.send_frame(BLE_CMD_SET_GROUP_META, payload)
        self._log(f"正在添加{name}第 {new_count} 页…")
        self._pending_select = (mode, new_count)
        self.after(400, lambda: self.controller.send_frame(BLE_CMD_GET_SLOT_STATUS, b""))

    def _delete_last_page(self, mode: int) -> None:
        """v1：只删最后一页（4003）。"""
        if not self.ui.connected:
            self.guide.configure(text=C.format_status("hint_disabled_connect"))
            return
        cur = self.ui.notes_count if mode == MODE_NOTES else self.ui.info_count
        if cur <= 0:
            self._log("没有可删的页")
            return
        name = "记事" if mode == MODE_NOTES else "信息"
        from tkinter import messagebox

        if not messagebox.askyesno("确认删除", f"将删除{name}第 {cur} 页（仅末页）。确定？"):
            return
        from nrf_console.protocol.cmds import BLE_CMD_DELETE_LAST_PAGE

        self.controller.send_frame(BLE_CMD_DELETE_LAST_PAGE, bytes([mode & 0xFF]))
        if self.ui.target and self.ui.target.mode == mode and self.ui.target.page == cur:
            self.ui.target = None
            self.ui.has_preview = False
        self._log(f"已请求删除{name}第 {cur} 页")
        self.after(500, lambda: self.controller.send_frame(BLE_CMD_GET_SLOT_STATUS, b""))

    def _on_save_overlays(self) -> None:
        """叠层开关 → 4002 → 设备持久化；数值由 MCU 填。"""
        if not self.ui.connected:
            self.guide.configure(text=C.format_status("hint_disabled_connect"))
            return
        flags = 0
        if self.overlay_pages.get():
            flags |= 0x01
        if self.overlay_batt.get():
            flags |= 0x02
        from nrf_console.protocol.cmds import BLE_CMD_SET_HOME_OVERLAY

        self.controller.send_frame(BLE_CMD_SET_HOME_OVERLAY, bytes([flags & 0xFF]))
        self._log(
            f"已发送叠层开关并重显首页：页数={'开' if flags & 1 else '关'} "
            f"电量={'开' if flags & 2 else '关'} — 请看墨水屏底部黑条"
        )
        self._refresh_home_preview()

    # ── advanced helpers ────────────────────────────────

    def _toggle_advanced(self) -> None:
        if self.adv_open.get():
            self.adv_frame.grid()
        else:
            self.adv_frame.grid_remove()

    def _on_allow_unconf(self) -> None:
        self.ui.allow_unconfigured = bool(self.allow_unconf.get())
        self._rebuild_page_list()
        self._refresh_all()

    def _on_ping(self) -> None:
        self.controller.send_frame(BLE_CMD_PING, b"")

    def _on_get_status(self) -> None:
        self.controller.send_frame(BLE_CMD_GET_SLOT_STATUS, b"")

    def _adv_set_notes(self) -> None:
        self._adv_set(MODE_NOTES, self.adv_notes_count, self.adv_notes_title)

    def _adv_set_info(self) -> None:
        self._adv_set(MODE_INFO, self.adv_info_count, self.adv_info_title)

    def _adv_set(self, mode: int, count_e, title_e) -> None:
        try:
            count = int(count_e.get().strip() or "0")
        except ValueError:
            self._log("页数须为整数")
            return
        title = utf8_fit(title_e.get(), GROUP_TITLE_MAX).decode("utf-8", errors="ignore")
        try:
            payload = pack_set_group_meta(mode, count, title)
        except ValueError as exc:
            self._log(str(exc))
            return
        self.controller.send_frame(BLE_CMD_SET_GROUP_META, payload)

    def _on_designer(self) -> None:
        sync_back(self.ink_cfg, self.app_cfg)
        bridge = write_nrf_console_bridge(self.ink_cfg)
        main_py = nrf_console_root() / "main.py"
        if not main_py.is_file():
            self._log(f"找不到设计器: {main_py}")
            return
        env = os.environ.copy()
        env["NRF_CONSOLE_CONFIG"] = str(bridge)
        subprocess.Popen([sys.executable, str(main_py)], cwd=str(main_py.parent), env=env)
        self._log("已打开 DIY 设计器")

    # ── refresh / events ────────────────────────────────

    def _refresh_all(self) -> None:
        step = current_step(self.ui)
        for s, lb in self._step_labels.items():
            if s == step:
                lb.configure(text_color=_PAPER, font=ctk.CTkFont(size=13, weight="bold"))
            else:
                lb.configure(text_color=_MUTED, font=ctk.CTkFont(size=13))

        self.status.configure(text=status_for_state(self.ui))
        edit = describe_target(self.ui.target)
        if self.ui.connected:
            banner = (
                f"编辑：{edit}  ·  设备：记事 {self.ui.notes_count} 页 · "
                f"信息 {self.ui.info_count} 页"
            )
        else:
            banner = "未连接 · 连接后显示记事/信息页数"
        self.page_banner.configure(text=banner)
        reason = push_block_reason(self.ui)
        ok = can_push(self.ui)
        self.btn_push.configure(state="normal" if ok else "disabled")
        self.btn_save.configure(state="normal" if ok else "disabled")
        self.guide.configure(
            text=reason
            if reason
            else f"目标：{edit} · 预览来源：{self._preview_src}"
        )

        busy = self.ui.busy
        self.btn_connect.configure(
            state="disabled" if (busy or self.ui.connected) else "normal"
        )
        self.btn_disc.configure(state="normal" if self.ui.connected and not busy else "disabled")

        # highlight selected page
        for (m, p), btn in self._page_btns.items():
            sel = self.ui.target and self.ui.target.mode == m and self.ui.target.page == p
            btn.configure(fg_color=_RED if sel else "#4a4740")

    def _log(self, msg: str) -> None:
        if hasattr(self, "log"):
            self.log.insert("end", msg + "\n")
            self.log.see("end")

    def _address_from_label(self, label: str) -> str:
        if not label or label.startswith("("):
            return ""
        parts = label.rsplit("  ", 1)
        if len(parts) == 2 and ":" in parts[1]:
            return parts[1].strip()
        for d in self._devices:
            if d.address in label:
                return d.address
        return self.app_cfg.last_address

    def _on_event(self, ev: UiEvent) -> None:
        if ev.kind == "log" and ev.message:
            self._log(ev.message)
        elif ev.kind == "error" and ev.message:
            self._log(f"ERR {ev.message}")
            msg = ev.message.lower()
            if "busy" in msg or "invalid_state" in msg or "status=-6" in msg:
                self.ui.status_key = "busy"
            self._refresh_all()
        elif ev.kind == "tx" and ev.message:
            self._log(ev.message)
        elif ev.kind == "rx" and ev.message:
            self._log(ev.message)
        elif ev.kind == "status" and ev.message:
            pass
        elif ev.kind == "slot_status" and isinstance(ev.data, dict):
            apply_slot_status(self.ui, ev.data)
            self.adv_notes_title.delete(0, "end")
            self.adv_notes_title.insert(0, self.ui.notes_title)
            self.adv_notes_count.delete(0, "end")
            self.adv_notes_count.insert(0, str(self.ui.notes_count))
            self.adv_info_title.delete(0, "end")
            self.adv_info_title.insert(0, self.ui.info_title)
            self.adv_info_count.delete(0, "end")
            self.adv_info_count.insert(0, str(self.ui.info_count))
            self._rebuild_page_list()
            if self.ui.status_key in ("reading", "connecting"):
                self.ui.status_key = "ready_pick_page"
            self._log(
                f"桌牌信息：记事{self.ui.notes_count}页 / 信息{self.ui.info_count}页"
            )
            pending = self._pending_select
            if pending is not None:
                self._pending_select = None
                mode, page = pending
                avail = pages_for_mode(self.ui, mode)
                if page in avail:
                    self._select_target(mode, page)
            if self.ui.target and self.ui.target.mode == MODE_HOME:
                self._refresh_home_preview()
            else:
                self._refresh_all()
        elif ev.kind == "busy" and isinstance(ev.data, dict):
            op = str(ev.data.get("op", "idle"))
            self.ui.busy = op != "idle"
            self.ui.connected = bool(ev.data.get("connected"))
            if op == "idle" and self.ui.connected and getattr(self, "_pending_show", None) is not None:
                self.ui.status_key = "ok_show" if self._pending_show else "ok_save"
                self._pending_show = None
            self._refresh_all()
        elif ev.kind == "connected":
            self.ui.connected = bool(ev.data)
            if self.ui.connected:
                self.ui.status_key = "reading"
                sync_back(self.ink_cfg, self.app_cfg)
                try:
                    save_config(self.ink_cfg)
                except Exception:  # noqa: BLE001
                    pass
                # 主动拉一次；固件也会推 8002
                self.after(400, lambda: self.controller.send_frame(BLE_CMD_GET_SLOT_STATUS, b""))
            else:
                self.ui.status_key = "disconnected"
                self.ui.target = None
                self.ui.has_preview = False
            self._refresh_all()
        elif ev.kind == "devices" and ev.data is not None:
            self._devices = list(ev.data)
            labels = [f"{d.name}  {d.address}" for d in self._devices]
            if not labels:
                labels = ["(无设备)"]
                self.ui.status_key = "no_device"
                self._one_click = False
            self.device_menu.configure(values=labels)
            self.device_menu.set(labels[0])
            if self._one_click and self._devices:
                self._one_click = False
                best = pick_strongest(self._devices)
                if best is not None:
                    self.ui.status_key = "connecting"
                    self._refresh_all()
                    self.controller.connect(best.address, best.name or "")
            else:
                self._refresh_all()

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
