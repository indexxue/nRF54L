# src/

本工程**产品编排**。平台起机在仓库 [`start/`](../../../start/)；Zigbee 协议在 [`../lib/zigbee/`](../lib/zigbee/)。

| 文件 | 职责 |
|------|------|
| `app.c` | `app_init()` / `app_run()`：按键、网络/控灯灯效、串口日志；调 `zigbee_app_*` |

## 启动顺序

1. `start_boot`（log / event / nvs / board / 可选 HFXO）
2. `app_init` → `zigbee_app_start()`
3. `app_run` — `start_poll` + 按键 + `zigbee_app_evt_fetch` 消费
