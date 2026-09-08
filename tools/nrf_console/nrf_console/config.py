"""Persistent app config (JSON)."""

from __future__ import annotations

import json
import os
from copy import deepcopy
from dataclasses import asdict, dataclass, field
from pathlib import Path
from typing import Any

DEFAULT_SVC_UUID = "53530120-0001-1000-8000-00805f9b34fb"
DEFAULT_RX_UUID = "53530120-0002-1000-8000-00805f9b34fb"
DEFAULT_TX_UUID = "53530120-0003-1000-8000-00805f9b34fb"


@dataclass
class AppConfig:
    transport: str = "ble_local"
    device_name_filter: str = "eink_tag"
    last_address: str = ""
    last_name: str = ""
    svc_uuid: str = DEFAULT_SVC_UUID
    rx_uuid: str = DEFAULT_RX_UUID
    tx_uuid: str = DEFAULT_TX_UUID
    auto_notify: bool = True
    scan_timeout_s: float = 8.0
    appearance_mode: str = "dark"
    color_theme: str = "blue"
    window_geometry: str = "960x640"
    presets: list[dict[str, Any]] = field(default_factory=list)

    def ensure_default_presets(self) -> None:
        if self.presets:
            return
        self.presets = [
            {"name": "PING", "cmd_id": 1001, "payload_hex": ""},
            {"name": "DISPLAY", "cmd_id": 2001, "payload_hex": ""},
        ]


def default_config_path() -> Path:
    override = os.environ.get("NRF_CONSOLE_CONFIG")
    if override:
        return Path(override)
    appdata = os.environ.get("APPDATA")
    if appdata:
        return Path(appdata) / "nrf_console" / "config.json"
    return Path.home() / ".nrf_console" / "config.json"


def load_config(path: Path | None = None) -> AppConfig:
    cfg_path = path or default_config_path()
    cfg = AppConfig()
    if cfg_path.is_file():
        raw = json.loads(cfg_path.read_text(encoding="utf-8"))
        known = {f.name for f in cfg.__dataclass_fields__.values()}  # type: ignore[attr-defined]
        for key, value in raw.items():
            if key in known:
                setattr(cfg, key, value)
    cfg.ensure_default_presets()
    return cfg


def save_config(cfg: AppConfig, path: Path | None = None) -> Path:
    cfg_path = path or default_config_path()
    cfg_path.parent.mkdir(parents=True, exist_ok=True)
    data = asdict(deepcopy(cfg))
    cfg_path.write_text(
        json.dumps(data, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    return cfg_path
