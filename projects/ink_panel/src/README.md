`app_init` 起板级、BLE 与心跳 work；`app_run` 只调度 `start_poll` + `app_poll`（按键 / BLE CMD）。板级见 `../board/`。
