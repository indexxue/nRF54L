"""Resolve sibling nrf_console and product docs (dev + PyInstaller frozen)."""

from __future__ import annotations

import sys
from pathlib import Path

_TOOL_ROOT = Path(__file__).resolve().parents[1]
_REPO_ROOT = _TOOL_ROOT.parents[1]


def _frozen() -> bool:
    return bool(getattr(sys, "frozen", False))


def tool_root() -> Path:
    if _frozen():
        return Path(sys.executable).resolve().parent
    return _TOOL_ROOT


def repo_root() -> Path:
    if _frozen():
        # Bundled release has no monorepo; keep exe dir as pseudo-root.
        return tool_root()
    return _REPO_ROOT


def nrf_console_root() -> Path:
    """Directory that contains the `nrf_console` Python package (or main.py)."""
    if _frozen():
        meipass = Path(getattr(sys, "_MEIPASS", tool_root()))
        # Packaged: nrf_console lives under _internal as a package.
        if (meipass / "nrf_console").is_dir():
            return meipass
        # Optional side-by-side folder next to the exe (DIY designer).
        side = tool_root() / "nrf_console"
        if (side / "main.py").is_file() or (side / "nrf_console").is_dir():
            return side
        return meipass
    return _REPO_ROOT / "tools" / "nrf_console"


def ensure_nrf_console_on_path() -> Path:
    """Insert sibling nrf_console so `import nrf_console` works (dev). Frozen: no-op."""
    if _frozen():
        return nrf_console_root()

    root = nrf_console_root()
    if not (root / "nrf_console").is_dir():
        raise FileNotFoundError(
            f"nrf_console not found at {root}. "
            "Clone/sync the monorepo or set association to tools/nrf_console."
        )
    s = str(root)
    if s not in sys.path:
        sys.path.insert(0, s)
    return root


def product_md() -> Path:
    if _frozen():
        return tool_root() / "docs" / "PRODUCT.md"
    return _REPO_ROOT / "tmp" / "ink_panel" / "PRODUCT.md"


def project_readme() -> Path:
    if _frozen():
        return tool_root() / "docs" / "ink_panel_README.md"
    return _REPO_ROOT / "projects" / "ink_panel" / "README.md"
