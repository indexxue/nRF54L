"""Host-side page templates → BWR planes."""

from .render import (
    NotesForm,
    InfoForm,
    render_info_door,
    render_notes_three,
    resolve_cjk_font,
)

__all__ = [
    "NotesForm",
    "InfoForm",
    "render_info_door",
    "render_notes_three",
    "resolve_cjk_font",
]
