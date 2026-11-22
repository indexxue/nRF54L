"""BLE connect / PING / plane upload for ink_panel."""

from __future__ import annotations

import asyncio
from collections.abc import Callable

from .config import InkPanelConfig
from .paths import ensure_nrf_console_on_path
from .session import iter_upload_steps

ensure_nrf_console_on_path()

from nrf_console.protocol.codec import decode_frame, encode_frame  # noqa: E402
from nrf_console.protocol.cmds import (  # noqa: E402
    BLE_CMD_ACK,
    BLE_CMD_PING,
    cmd_name,
    parse_ack_payload,
)
from nrf_console.transport import create_transport  # noqa: E402


async def _wait_ack(
    queue: asyncio.Queue[tuple[int, int]], cmd_id: int, timeout_s: float
) -> int:
    deadline = asyncio.get_running_loop().time() + timeout_s
    while True:
        remaining = deadline - asyncio.get_running_loop().time()
        if remaining <= 0:
            raise TimeoutError(f"ACK timeout for {cmd_name(cmd_id)}({cmd_id})")
        req, status = await asyncio.wait_for(queue.get(), timeout=remaining)
        if req == cmd_id:
            return status


class InkPanelClient:
    def __init__(self, cfg: InkPanelConfig) -> None:
        self.cfg = cfg
        self.transport = create_transport(cfg.transport)
        self._ack_q: asyncio.Queue[tuple[int, int]] = asyncio.Queue()

    async def _on_notify(self, data: bytes) -> None:
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
        await self._ack_q.put((int(ack["req_cmd_id"]), int(ack["status"])))

    async def scan(self) -> list:
        return await self.transport.scan(
            timeout_s=self.cfg.scan_timeout_s,
            name_filter=self.cfg.device_name_filter,
        )

    async def connect(self, address: str | None = None) -> str:
        addr = address or self.cfg.last_address
        if not addr:
            devices = await self.scan()
            if not devices:
                raise RuntimeError(
                    f"no devices matching filter={self.cfg.device_name_filter!r}"
                )
            for d in devices:
                print(f"  {d.name}  {d.address}  rssi={d.rssi}")
            addr = devices[0].address
            print(f"using {addr}")
        await self.transport.connect(
            addr,
            svc_uuid=self.cfg.svc_uuid,
            rx_uuid=self.cfg.rx_uuid,
            tx_uuid=self.cfg.tx_uuid,
            auto_notify=True,
            on_notify=self._on_notify,
        )
        self.cfg.last_address = addr
        return addr

    async def disconnect(self) -> None:
        await self.transport.disconnect()

    async def ping(self) -> int:
        await self.transport.write(encode_frame(BLE_CMD_PING, b""))
        return await _wait_ack(self._ack_q, BLE_CMD_PING, self.cfg.ack_timeout_s)

    async def upload(
        self,
        bw: bytes,
        red: bytes,
        *,
        mode: int = 0,
        page_id: int = 0,
        show: bool = True,
        progress: Callable[[int, int, str], None] | None = None,
    ) -> None:
        max_frame = self.transport.max_write_size()
        payload_cap = max_frame - 4
        if payload_cap < 18:
            raise RuntimeError(
                f"ATT write max {max_frame} B too small for DISP_BEGIN"
            )
        chunk_data_max = min(235, max(1, payload_cap - 5))
        steps = list(
            iter_upload_steps(
                bw,
                red,
                mode=mode,
                page_id=page_id,
                show=show,
                with_slot=self.cfg.begin_with_slot,
                chunk_data_max=chunk_data_max,
            )
        )
        total = len(steps)
        for i, (cmd_id, payload) in enumerate(steps, start=1):
            await self.transport.write(encode_frame(cmd_id, payload))
            status = await _wait_ack(self._ack_q, cmd_id, self.cfg.ack_timeout_s)
            if status != 0:
                raise RuntimeError(
                    f"FAIL {i}/{total} {cmd_name(cmd_id)} status={status}"
                )
            if progress and (i == 1 or i == total or (i % 20) == 0):
                progress(i, total, cmd_name(cmd_id))
