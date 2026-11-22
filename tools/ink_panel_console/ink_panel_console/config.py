"""ink_panel defaults; optional JSON under %APPDATA%/ink_panel_console."""

from __future__ import annotations

import json
import os
from dataclasses import asdict, dataclass
from pathlib import Path

from .paths import ensure_nrf_console_on_path

ensure_nrf_console_on_path()

from nrf_console.config import (  # noqa: E402
    DEFAULT_RX_UUID,
    DEFAULT_SVC_UUID,
    DEFAULT_TX_UUID,
    uuids_from_sn,
)

DEVICE_NAME = "ink_panel"
PANEL_WIDTH = 400
PANEL_HEIGHT = 300
PLANE_SIZE = 15000  # 400×300 / 8

# α firmware：REFRESH 固定写入 home/0；β 起 BEGIN 带 mode/page
MODE_HOME = 0
MODE_NOTES = 1
MODE_INFO = 2

MODE_NAMES = {
    MODE_HOME: "home",
    MODE_NOTES: "notes",
    MODE_INFO: "info",
}


@dataclass
class InkPanelConfig:
    transport: str = "ble_local"
    device_name_filter: str = DEVICE_NAME
    last_address: str = ""
    last_name: str = ""
    svc_uuid: str = DEFAULT_SVC_UUID
    rx_uuid: str = DEFAULT_RX_UUID
    tx_uuid: str = DEFAULT_TX_UUID
    scan_timeout_s: float = 8.0
    ack_timeout_s: float = 12.0
    # α=False：BEGIN 仍 18B；β=True：追加 mode/page（固件未升前勿开）
    begin_with_slot: bool = False

    def apply_sn(self, sn: str) -> None:
        self.svc_uuid, self.rx_uuid, self.tx_uuid = uuids_from_sn(sn)


def default_config_path() -> Path:
    override = os.environ.get("INK_PANEL_CONSOLE_CONFIG")
    if override:
        return Path(override)
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "ink_panel_console" / "config.json"
    return Path.home() / ".ink_panel_console" / "config.json"


def load_config(path: Path | None = None) -> InkPanelConfig:
    cfg_path = path or default_config_path()
    cfg = InkPanelConfig()
    if cfg_path.is_file():
        raw = json.loads(cfg_path.read_text(encoding="utf-8"))
        known = set(cfg.__dataclass_fields__)  # type: ignore[attr-defined]
        for key, value in raw.items():
            if key in known:
                setattr(cfg, key, value)
    return cfg


def save_config(cfg: InkPanelConfig, path: Path | None = None) -> Path:
    cfg_path = path or default_config_path()
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    cfg_path.write_text(
        json.dumps(asdict(cfg), indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return cfg_path


def nrf_console_bridge_config_path() -> Path:
    """Config file handed to nrf_console designer via NRF_CONSOLE_CONFIG."""
    return default_config_path().parent / "nrf_console_bridge.json"


def write_nrf_console_bridge(cfg: InkPanelConfig) -> Path:
    """Point nrf_console at ink_panel device name / UUIDs for 联调."""
    path = nrf_console_bridge_config_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    data = {
        "transport": cfg.transport,
        "device_name_filter": cfg.device_name_filter or DEVICE_NAME,
        "last_address": cfg.last_address,
        "last_name": cfg.last_name,
        "svc_uuid": cfg.svc_uuid,
        "rx_uuid": cfg.rx_uuid,
        "tx_uuid": cfg.tx_uuid,
        "auto_notify": True,
        "scan_timeout_s": cfg.scan_timeout_s,
        "appearance_mode": "dark",
        "color_theme": "blue",
        "window_geometry": "1100x720",
        "presets": [
            {"name": "PING", "cmd_id": 1001, "payload_hex": ""},
            {"name": "DISPLAY", "cmd_id": 2001, "payload_hex": ""},
        ],
    }
    path.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return path
