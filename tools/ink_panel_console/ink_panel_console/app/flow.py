"""步骤与可推送条件（与控件解耦）。"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto

from ink_panel_console.app.copy import (
    GROUP_HOME,
    GROUP_INFO,
    GROUP_NOTES,
    format_status,
    page_label,
)
from ink_panel_console.config import (
    INFO_CONTENT_MAX,
    MODE_HOME,
    MODE_INFO,
    MODE_NOTES,
    NOTES_CONTENT_MAX,
)

DEFAULT_NOTES_ENABLE = 1  # 空组首次只加 1 页，再点「添加一页」
DEFAULT_INFO_ENABLE = 1


class Step(Enum):
    CONNECT = auto()
    PAGE = auto()
    EDIT = auto()
    PUSH = auto()


@dataclass
class Target:
    mode: int
    page: int

    @property
    def group_name(self) -> str:
        if self.mode == MODE_NOTES:
            return GROUP_NOTES
        if self.mode == MODE_INFO:
            return GROUP_INFO
        return GROUP_HOME


@dataclass
class UiState:
    connected: bool = False
    busy: bool = False
    status_key: str = "idle"
    target: Target | None = None
    has_preview: bool = False
    notes_count: int = 0
    info_count: int = 0
    notes_title: str = ""
    info_title: str = ""
    allow_unconfigured: bool = False
    last_ok_show: bool | None = None


def content_max(mode: int) -> int:
    if mode == MODE_NOTES:
        return NOTES_CONTENT_MAX
    if mode == MODE_INFO:
        return INFO_CONTENT_MAX
    return 0


def default_enable_count(mode: int) -> int:
    if mode == MODE_NOTES:
        return DEFAULT_NOTES_ENABLE
    if mode == MODE_INFO:
        return DEFAULT_INFO_ENABLE
    return 0


def pages_for_mode(state: UiState, mode: int) -> list[int]:
    if mode == MODE_HOME:
        return [0]
    count = state.notes_count if mode == MODE_NOTES else state.info_count
    if state.allow_unconfigured:
        return list(range(1, content_max(mode) + 1))
    if count <= 0:
        return []
    return list(range(1, count + 1))


def push_block_reason(state: UiState) -> str | None:
    if state.busy:
        return format_status("pushing")
    if not state.connected:
        return format_status("hint_disabled_connect")
    if state.target is None:
        return format_status("hint_disabled_page")
    if not state.has_preview:
        return format_status("hint_disabled_content")
    return None


def can_push(state: UiState) -> bool:
    return push_block_reason(state) is None


def current_step(state: UiState) -> Step:
    if not state.connected:
        return Step.CONNECT
    if state.target is None:
        return Step.PAGE
    if not state.has_preview:
        return Step.EDIT
    return Step.PUSH


def status_for_state(state: UiState) -> str:
    if state.status_key in ("scanning", "connecting", "reading", "pushing", "no_device"):
        return format_status(state.status_key)
    if state.status_key in ("ok_show", "ok_save") and state.target is not None:
        return format_status(
            state.status_key,
            group=state.target.group_name,
            n=state.target.page if state.target.mode != MODE_HOME else 1,
        )
    if state.status_key == "disconnected":
        return format_status("disconnected")
    if not state.connected:
        return format_status("idle")
    if state.target is None:
        return format_status("ready_pick_page")
    if not state.has_preview:
        return format_status("need_content")
    return format_status(
        "ready_push",
        group=state.target.group_name,
        n=state.target.page if state.target.mode != MODE_HOME else 1,
    )


def apply_slot_status(state: UiState, ss: dict) -> None:
    state.notes_count = int(ss.get("notes_count", 0))
    state.info_count = int(ss.get("info_count", 0))
    state.notes_title = str(ss.get("notes_title") or "")
    state.info_title = str(ss.get("info_title") or "")


def pick_strongest(devices: list) -> object | None:
    if not devices:
        return None
    return sorted(
        devices,
        key=lambda d: (d.rssi is None, -(d.rssi or -999), (d.name or "").lower()),
    )[0]


def describe_target(target: Target | None) -> str:
    if target is None:
        return "（未选）"
    if target.mode == MODE_HOME:
        return page_label(GROUP_HOME, 0)
    return page_label(target.group_name, target.page)
