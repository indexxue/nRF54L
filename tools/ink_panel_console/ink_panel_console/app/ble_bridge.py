"""Adapt InkPanelConfig → nrf_console BleController."""

from __future__ import annotations

from ink_panel_console.config import InkPanelConfig
from ink_panel_console.paths import ensure_nrf_console_on_path

ensure_nrf_console_on_path()

from nrf_console.app.controller import BleController, UiEvent  # noqa: E402
from nrf_console.config import AppConfig  # noqa: E402
from nrf_console.protocol.cmds import BLE_CMD_PING  # noqa: E402


def to_app_config(cfg: InkPanelConfig) -> AppConfig:
    return AppConfig(
        transport=cfg.transport,
        device_name_filter=cfg.device_name_filter,
        last_address=cfg.last_address,
        last_name=cfg.last_name,
        svc_uuid=cfg.svc_uuid,
        rx_uuid=cfg.rx_uuid,
        tx_uuid=cfg.tx_uuid,
        auto_notify=True,
        scan_timeout_s=cfg.scan_timeout_s,
        appearance_mode="dark",
        color_theme="dark-blue",
        window_geometry="1200x820",
    )


def sync_back(ink: InkPanelConfig, app: AppConfig) -> None:
    ink.last_address = app.last_address
    ink.last_name = app.last_name
    ink.device_name_filter = app.device_name_filter
    ink.svc_uuid = app.svc_uuid
    ink.rx_uuid = app.rx_uuid
    ink.tx_uuid = app.tx_uuid


__all__ = ["BleController", "UiEvent", "BLE_CMD_PING", "to_app_config", "sync_back"]
