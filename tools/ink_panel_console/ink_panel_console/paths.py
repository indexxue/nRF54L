"""Resolve sibling nrf_console and product docs."""

from __future__ import annotations

from pathlib import Path

_TOOL_ROOT = Path(__file__).resolve().parents[1]
_REPO_ROOT = _TOOL_ROOT.parents[1]


def tool_root() -> Path:
    return _TOOL_ROOT


def repo_root() -> Path:
    return _REPO_ROOT


def nrf_console_root() -> Path:
    return _REPO_ROOT / "tools" / "nrf_console"


def ensure_nrf_console_on_path() -> Path:
    """Insert sibling nrf_console so `import nrf_console` works."""
    import sys

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
    return _REPO_ROOT / "tmp" / "ink_panel" / "PRODUCT.md"


def project_readme() -> Path:
    return _REPO_ROOT / "projects" / "ink_panel" / "README.md"
