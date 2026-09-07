"""Launch HTML DIY designer in pywebview with BLE bridge."""

from __future__ import annotations

from pathlib import Path

from nrf_console.config import AppConfig, save_config
from nrf_console.designer.bridge import DesignerApi

STATIC = Path(__file__).resolve().parent / "static" / "index.html"


def run_designer(cfg: AppConfig) -> None:
    try:
        import webview
    except ImportError as exc:  # pragma: no cover
        raise SystemExit(
            "pywebview is required for the DIY designer.\n"
            "  pip install pywebview\n"
            f"({exc})"
        ) from exc

    if not STATIC.is_file():
        raise SystemExit(f"designer HTML missing: {STATIC}")

    api = DesignerApi(cfg)
    api.start()

    window = webview.create_window(
        "EPD Tag Designer",
        url=STATIC.as_uri(),
        js_api=api,
        width=1280,
        height=860,
        min_size=(960, 640),
    )

    def _on_closed() -> None:
        try:
            save_config(cfg)
        except Exception:  # noqa: BLE001
            pass
        api.stop()

    window.events.closed += _on_closed
    webview.start(debug=False)
