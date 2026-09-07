"""EPD host helpers: plane pack + display framework + BLE upload session."""

from .display import Canvas, FontSize
from .image_import import image_to_planes
from .pages import (
    FIELD_LAYOUTS,
    FIXED_LAYOUTS,
    PAGE_CATALOG,
    build_page,
    build_tag,
    make_gfx_demo,
    make_smoke_bwr,
    make_tag_demo,
    make_test_pattern,
)
from .plane import PLANE_SIZE, Color
from .session import iter_upload_steps, pack_begin

__all__ = [
    "FIELD_LAYOUTS",
    "FIXED_LAYOUTS",
    "PLANE_SIZE",
    "PAGE_CATALOG",
    "Canvas",
    "Color",
    "FontSize",
    "build_page",
    "build_tag",
    "image_to_planes",
    "iter_upload_steps",
    "make_gfx_demo",
    "make_smoke_bwr",
    "make_tag_demo",
    "make_test_pattern",
    "pack_begin",
]
