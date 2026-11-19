# common/

跨 `projects/*` 共享的可移植源码（经 [`modules/nrf54_common`](../modules/nrf54_common) 编进 Zephyr）。

**归属**：只有 **多于一个** 工程使用的代码放这里；单工程逻辑放 `projects/<name>/lib/` 或 `source/`。

规范见 [`doc/dev_norms.md`](../doc/dev_norms.md)。Phase 1A：唤醒用 `k_event`，CRC 用 `crc32_ieee`；**不**再维护自研 `event` / `crc32` / `input`。

| Module | Header | Source | Notes |
|--------|--------|--------|-------|
| types | `include/type.h` | — | `status_t` 等 |
| app_event | `include/app_event.h` | — | `APP_EVT_*` + `extern app_events`；`K_EVENT_DEFINE` 在 `start.c` |
| log | `include/log.h` | `src/log.c` | `k_mutex` + `printk`（暂不迁 Zephyr LOG） |
| fw_version | `include/fw_version.h` | `src/fw_version.c` | `fw_version_get_string()` ← `CONFIG_APP_VERSION_STRING` |
| flexible_button | `include/flexible_button.h` | `src/flexible_button.c` | 按键 FSM；经 `button` 使用 |
| flash_partition | `include/flash_partition.h` | — | 对照 [`doc/flash_layout.md`](../doc/flash_layout.md) |
| nvs | `include/nvs.h` | `src/nvs.c` | **Settings 业务壳**（`ty_app/*`），非自研 NVS |
| device_profile | `include/device_profile.h` | `src/device_profile.c` | `product_id` → SKU 名；`hardware_id` → 板名 |
| board | `include/board.h` | `src/board.c` | SoC 外设表 + mask；片外器件不进此表 |
| button | `include/button.h` | `src/button.c` | 业务按键；FTM 完成回调由 `start` 写 boot_mode |
| led_scene | `include/led_scene.h` | `src/led_scene.c` | LED 场景；GPIO 来自 `board_config` |

**不在 common**：factory `cmd`；片外 EPD/NFC/CAP1203 等 → 各工程 `source/` / `board/`。

## 起机用法（经 start/）

```c
/* start_boot 内部顺序摘要 */
log_init → nvs_init → board_init
→（可选）led_scene_init + BOOTUP
→ button_init + button_ftm_done_cb_set(…)
/* 主环 */
start_poll(); /* led_scene_update + button_schedule + button_ftm_seq_poll */
```

唤醒位：

```c
#include "app_event.h"
k_event_post(&app_events, APP_EVT_BUTTON);
(void)app_event_take(APP_EVT_BUTTON);
```

## product_id（device_profile / nvs）

| 工程 | 宏 | 默认 `CONFIG_NRF54_NVS_DEFAULT_DEVICE_ID` |
|------|----|------------------------------------------|
| factory | `NVS_DEVICE_ID_DEFAULT` | `0x0`（读 NVS `did`） |
| demo | `NVS_PROJECT_ID_DEMO` | `0x53530110` |
| eink_tag | `NVS_PROJECT_ID_EINK_TAG` | `0x53530120` |
| ink_panel | `NVS_PROJECT_ID_INK_PANEL` | `0x53530130` |
| zigbee_ed | `NVS_PROJECT_ID_ZIGBEE_ED` | `0x53530140` |
| zigbee_router | `NVS_PROJECT_ID_ZIGBEE_ROUTER` | `0x53530150` |

新产品：在 `nvs.h` 的 `NVS_PROJECT_ID_*` + `NVS_PROJECT_ID_TABLE` 加一行，并同步 `device_profile.c` 与工程 `prj.conf`。

## Settings

```
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_CRC=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y
```

业务禁止散落 `settings_save_one`；走 `nvs_*`。

**SN**：无键时首启生成 `T90M` + 8 hex 并落盘。

Do **not** include `zephyr/logging/log.h` together with `log.h`（宏冲突）。

**Release**：`nrf.cmd release` → `CONFIG_APP_BUILD_RELEASE=y`，`log_output()` 为空操作。
