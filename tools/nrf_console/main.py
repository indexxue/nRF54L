"""nRF Console / EPD Tag Designer entry point."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from nrf_console.config import load_config


def main() -> int:
    p = argparse.ArgumentParser(description="EPD Tag Designer (DIY + BLE)")
    p.add_argument(
        "--classic",
        action="store_true",
        help="legacy CustomTkinter console UI",
    )
    args = p.parse_args()
    cfg = load_config()
    if args.classic:
        from nrf_console.app import run_app

        run_app(cfg)
    else:
        from nrf_console.designer import run_designer

        run_designer(cfg)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
