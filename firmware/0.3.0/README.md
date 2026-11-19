# firmware 0.3.0 — Zephyr-native common Phase 1A

| 项 | 值 |
|----|-----|
| 版本 | **0.3.0** |
| 打包日 | 20261119 |
| 签名 | unsigned |
| Tag | `v0.3.0` |

## 本版要点

- **common**：自研 `event` / `crc32` / `input` 删除；唤醒改 `k_event`（`app_event.h`），CRC 改 `crc32_ieee`
- **product_id**：补齐 `ink_panel` / `zigbee_ed` / `zigbee_router`（zb 不再共用 `0x…130`）
- **button**：FTM 跳槽回调迁 `start`；浅睡唤醒清 stale last 事件
- Settings 仍经 `nvs_*`；`log` / `button` FSM / `led_scene` 本版未迁

## 产物（本机 `nrf.cmd release` 生成，二进制默认不入库）

优先烧 **merged `.hex`**（含 MCUBoot 时）。

| 工程 | 示例文件名 |
|------|------------|
| ink_panel | `nrf54l15_ink_panel_0.3.0_20261119_unsigned.hex` |
| demo | `nrf54l15_demo_0.3.0_20261119_unsigned.hex` |
| factory | `nrf54l15_factory_0.3.0_20261119_unsigned.hex`（slot1 / Direct-XIP） |
| eink_tag | `nrf54l15_eink_tag_0.3.0_20261119_unsigned.hex` |
| zigbee_ed | `nrf54l15_zigbee_ed_0.3.0_20261119_unsigned.hex` |
| zigbee_router | `nrf54l15_zigbee_router_0.3.0_20261119_unsigned.hex` |

```powershell
.\nrf.cmd -Project ink_panel release 0.3.0
.\nrf.cmd -Project ink_panel flash
.\nrf.cmd -Project factory flash
.\nrf.cmd boot-mode app
```

## 迁移注意

- 应用侧勿再 include `event.h` / `crc32.h` / `input.h`
- 已量产 zb 板若 NVS `did` 仍为 `0x53530130`，需改写为 `0x53530140`（ed）或 `0x53530150`（router）
