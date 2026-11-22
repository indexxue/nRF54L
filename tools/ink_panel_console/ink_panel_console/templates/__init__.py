"""Host-side page templates → BWR planes."""

from .render import (
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

__all__ = [
    "NOTES_ITEMS_MAX",
    "NOTES_ITEMS_MIN",
    "InfoForm",
    "NotesForm",
    "NotesItem",
    "compose_home_overlay",
    "render_blank_home",
    "render_info_door",
    "render_notes_three",
    "resolve_cjk_font",
]
