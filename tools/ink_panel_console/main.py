"""ink_panel_console — 默认可视化联调台；CLI 用 `python main.py cli …`。"""

from __future__ import annotations

import argparse
import asyncio
import os
import subprocess
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from ink_panel_console.config import (
    MODE_NAMES,
    PANEL_HEIGHT,
    PANEL_WIDTH,
    load_config,
    save_config,
    write_nrf_console_bridge,
)
from ink_panel_console.pages import PAGE_BUILDERS, PAGE_DEFAULT_SLOT
from ink_panel_console.paths import nrf_console_root, product_md, project_readme


def run_gui() -> int:
    try:
        from ink_panel_console.app import run_app
    except ImportError as exc:
        print(
            "GUI needs customtkinter (+ bleak for BLE).\n"
            "  pip install -r requirements.txt\n"
            f"({exc})"
        )
        return 1
    run_app(load_config())
    return 0


def _parse_mode(s: str) -> int:
    s = s.strip().lower()
    for k, v in MODE_NAMES.items():
        if s == v or s == str(k):
            return k
    raise argparse.ArgumentTypeError(f"mode must be home|notes|info or 0|1|2, got {s!r}")


async def cmd_scan(args: argparse.Namespace) -> int:
    from ink_panel_console.client import InkPanelClient

    cfg = load_config()
    if args.filter:
        cfg.device_name_filter = args.filter
    client = InkPanelClient(cfg)
    print(f"scanning filter={cfg.device_name_filter!r}…")
    devices = await client.scan()
    if not devices:
        print("no devices")
        return 1
    for d in devices:
        print(f"  {d.name}  {d.address}  rssi={d.rssi}")
    return 0


async def cmd_ping(args: argparse.Namespace) -> int:
    from ink_panel_console.client import InkPanelClient

    cfg = load_config()
    client = InkPanelClient(cfg)
    try:
        addr = await client.connect(args.address)
        st = await client.ping()
        print(f"PING -> status={st} ({addr})")
        save_config(cfg)
        return 0 if st == 0 else 2
    finally:
        await client.disconnect()


async def cmd_upload(args: argparse.Namespace) -> int:
    from ink_panel_console.client import InkPanelClient
    from ink_panel_console.panel import image_to_planes

    cfg = load_config()
    if args.begin_slot:
        cfg.begin_with_slot = True

    mode = _parse_mode(args.mode) if args.mode else None
    page_id = args.page_id

    if args.image:
        print(f"import image {args.image} → {PANEL_WIDTH}x{PANEL_HEIGHT}")
        bw, red = image_to_planes(args.image)
        if mode is None:
            mode = 0
        if page_id is None:
            page_id = 0
    else:
        builder = PAGE_BUILDERS[args.page]
        bw, red = builder()
        default_mode, default_page = PAGE_DEFAULT_SLOT[args.page]
        if mode is None:
            mode = default_mode
        if page_id is None:
            page_id = default_page

    if not cfg.begin_with_slot:
        print("WARN: begin_with_slot=false → 18B BEGIN（ink_panel β 固件会拒）")

    client = InkPanelClient(cfg)
    try:
        addr = await client.connect(args.address)
        print(
            f"upload {PANEL_WIDTH}x{PANEL_HEIGHT} mode={MODE_NAMES.get(mode, mode)} "
            f"page={page_id} → {addr}"
        )

        def progress(i: int, total: int, name: str) -> None:
            print(f"  {i}/{total} {name} ok")

        await client.upload(
            bw, red, mode=mode, page_id=page_id, show=not args.no_show, progress=progress
        )
        print("done")
        save_config(cfg)
        return 0
    except Exception as exc:  # noqa: BLE001
        print(f"error: {exc}")
        return 2
    finally:
        await client.disconnect()


def cmd_designer(args: argparse.Namespace) -> int:
    cfg = load_config()
    bridge = write_nrf_console_bridge(cfg)
    nrf_root = nrf_console_root()
    main_py = nrf_root / "main.py"
    if not main_py.is_file():
        print(f"nrf_console missing: {main_py}")
        return 1
    env = os.environ.copy()
    env["NRF_CONSOLE_CONFIG"] = str(bridge)
    cmd = [sys.executable, str(main_py)]
    if args.classic:
        cmd.append("--classic")
    return subprocess.call(cmd, cwd=str(nrf_root), env=env)


def cmd_info(_: argparse.Namespace) -> int:
    cfg = load_config()
    print("ink_panel_console (GUI default)")
    print(f"  panel:      {PANEL_WIDTH}x{PANEL_HEIGHT} BWR")
    print(f"  filter:     {cfg.device_name_filter}")
    print(f"  nrf_console:{nrf_console_root()}")
    print(f"  firmware:   {project_readme()}")
    print(f"  product:    {product_md()}")
    return 0


def build_cli_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(description="ink_panel CLI（无头用）")
    sub = p.add_subparsers(dest="cmd", required=True)

    p_scan = sub.add_parser("scan")
    p_scan.add_argument("--filter", default="")
    p_scan.set_defaults(func=lambda a: asyncio.run(cmd_scan(a)))

    p_ping = sub.add_parser("ping")
    p_ping.add_argument("--address")
    p_ping.set_defaults(func=lambda a: asyncio.run(cmd_ping(a)))

    p_up = sub.add_parser("upload")
    p_up.add_argument("--page", default="bands", choices=sorted(PAGE_BUILDERS.keys()))
    p_up.add_argument("--image")
    p_up.add_argument("--mode")
    p_up.add_argument("--page-id", type=int, default=None)
    p_up.add_argument("--address")
    p_up.add_argument("--begin-slot", action="store_true")
    p_up.add_argument("--no-show", action="store_true")
    p_up.set_defaults(func=lambda a: asyncio.run(cmd_upload(a)))

    p_des = sub.add_parser("designer", help="仅启动 nrf_console")
    p_des.add_argument("--classic", action="store_true")
    p_des.set_defaults(func=cmd_designer)

    p_info = sub.add_parser("info")
    p_info.set_defaults(func=cmd_info)
    return p


def main() -> int:
    argv = sys.argv[1:]
    if not argv:
        return run_gui()
    if argv[0] in ("--cli", "cli"):
        parser = build_cli_parser()
        args = parser.parse_args(argv[1:])
        return int(args.func(args))
    if argv[0] in ("-h", "--help"):
        print(
            "ink_panel_console\n"
            "  python main.py              # 可视化联调台（默认）\n"
            "  python main.py cli scan|ping|upload|designer|info\n"
            "  python main.py --help\n"
        )
        return 0
    # bare subcommand without `cli` prefix still works
    if argv[0] in ("scan", "ping", "upload", "designer", "info"):
        parser = build_cli_parser()
        args = parser.parse_args(argv)
        return int(args.func(args))
    print(f"unknown args: {argv}; try python main.py --help")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
