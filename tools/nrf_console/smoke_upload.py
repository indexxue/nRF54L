"""Headless EPD smoke upload over BLE (no GUI).

Usage (from tools/nrf_console, venv active):

    python smoke_upload.py
    python smoke_upload.py --address AA:BB:CC:DD:EE:FF
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))

from nrf_console.config import load_config
from nrf_console.epd import PAGE_CATALOG, build_page, iter_upload_steps
from nrf_console.protocol.codec import decode_frame, encode_frame
from nrf_console.protocol.cmds import (
    BLE_CMD_ACK,
    cmd_name,
    parse_ack_payload,
)
from nrf_console.transport import create_transport


async def _wait_ack(queue: asyncio.Queue[tuple[int, int]], cmd_id: int, timeout_s: float) -> int:
    deadline = asyncio.get_running_loop().time() + timeout_s
    while True:
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            raise TimeoutError(f"ACK timeout for {cmd_name(cmd_id)}({cmd_id})")
        req, status = await asyncio.wait_for(queue.get(), timeout=remaining)
        if req == cmd_id:
            return status
        # ignore stale / unmatched ACKs


async def run(address: str | None, name_filter: str, timeout_s: float, page: str) -> int:
    cfg = load_config()
    transport = create_transport(cfg.transport)
    ack_q: asyncio.Queue[tuple[int, int]] = asyncio.Queue()

    async def on_notify(data: bytes) -> None:
        try:
            frame = decode_frame(data)
        except ValueError:
            return
        if frame.cmd_id != BLE_CMD_ACK:
            return
        try:
            ack = parse_ack_payload(frame.payload)
        except ValueError:
            return
        await ack_q.put((int(ack["req_cmd_id"]), int(ack["status"])))

    if not address:
        print(f"scanning (filter={name_filter!r})…")
        devices = await transport.scan(timeout_s=cfg.scan_timeout_s, name_filter=name_filter)
        if not devices:
            print("no devices found")
            return 1
        for d in devices:
            print(f"  {d.name}  {d.address}  rssi={d.rssi}")
        address = devices[0].address
        print(f"using {address}")

    print(f"connecting {address}…")
    await transport.connect(
        address,
        svc_uuid=cfg.svc_uuid,
        rx_uuid=cfg.rx_uuid,
        tx_uuid=cfg.tx_uuid,
        auto_notify=True,
        on_notify=on_notify,
    )

    try:
        bw, red = build_page(page)
        max_frame = transport.max_write_size()
        payload_cap = max_frame - 4
        if payload_cap < 18:
            print(
                f"ATT write max {max_frame} B too small for DISP_BEGIN; "
                "rebuild firmware with larger BT_L2CAP_TX_MTU"
            )
            return 3
        chunk_data_max = min(235, max(1, payload_cap - 5))
        steps = list(iter_upload_steps(bw, red, chunk_data_max=chunk_data_max))
        print(
            f"upload page={page!r} {len(steps)} frames "
            f"(wor≤{max_frame}, chunk_data≤{chunk_data_max})…"
        )
        for i, (cmd_id, payload) in enumerate(steps, start=1):
            await transport.write(encode_frame(cmd_id, payload))
            status = await _wait_ack(ack_q, cmd_id, timeout_s)
            if status != 0:
                print(f"FAIL {i}/{len(steps)} {cmd_name(cmd_id)} status={status}")
                return 2
            if i == 1 or i == len(steps) or (i % 10) == 0:
                print(f"  {i}/{len(steps)} {cmd_name(cmd_id)} ok")
        print("done — panel should refresh asynchronously")
        return 0
    finally:
        await transport.disconnect()


def main() -> int:
    p = argparse.ArgumentParser(description="BLE EPD plane-upload smoke test")
    p.add_argument("--address", help="BLE address; default = first scan hit")
    p.add_argument("--filter", default="eink_tag", help="scan name filter")
    p.add_argument("--ack-timeout", type=float, default=8.0, help="ACK timeout seconds")
    p.add_argument(
        "--page",
        default="smoke",
        choices=sorted(PAGE_CATALOG.keys()),
        help="page layout to upload",
    )
    args = p.parse_args()
    return asyncio.run(run(args.address, args.filter, args.ack_timeout, args.page))


if __name__ == "__main__":
    raise SystemExit(main())
