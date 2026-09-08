"""Resolve package paths for source runs and PyInstaller frozen builds."""

from __future__ import annotations

import sys
from pathlib import Path


def package_dir() -> Path:
    """Directory that contains the ``nrf_console`` package modules."""
    if getattr(sys, "frozen", False) and hasattr(sys, "_MEIPASS"):
        return Path(sys._MEIPASS) / "nrf_console"
    return Path(__file__).resolve().parent


def designer_index_html() -> Path:
    return package_dir() / "designer" / "static" / "index.html"
