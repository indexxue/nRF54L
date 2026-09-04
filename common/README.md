# common/

Portable shared sources for apps under `projects/` (API aligned with ESP32/STM32 Common).

**Ownership**: only code used by **more than one** app belongs here. Single-app code (e.g. factory `cmd`, buzzer) goes under `projects/<name>/lib/<module>/`.

Zephyr packaging lives under [`../modules/nrf54_common`](../modules/nrf54_common) — this tree has **no** `zephyr/` folder.

| Module | Header | Source | Origin | Notes |
|--------|--------|--------|--------|-------|
| types | `include/type.h` | — | ESP32 `type.h` | `status_t` without `esp_err` |
| log | `include/log.h` | `src/log.c` | ESP/STM32 log | `k_mutex` + default `printk` |
| version | `include/fw_version.h` | `src/fw_version.c` | new | `CONFIG_APP_VERSION_STRING`；勿用 `version.h`（与 Zephyr 冲突） |
| flexible_button | `include/flexible_button.h` | `src/flexible_button.c` | ESP/STM32 | GPIO via read callback |
| crc32 | `include/crc32.h` | `src/crc32.c` | STM32G431 | buffer CRC only |
| event | `include/event.h` | `src/event.c` | STM32 event | `k_sem` / `k_mutex` |
| flash_partition | `include/flash_partition.h` | — | nRF layout | 与 [`doc/flash_layout.md`](../doc/flash_layout.md) 一致 |
| nvs | `include/nvs.h` | `src/nvs.c` | ESP nvs N2 精简 | Zephyr Settings；`did`/`hid`/`mac`/`sn`/`dtype`/`bmode` |
| device_profile | `include/device_profile.h` | `src/device_profile.c` | ESP profile | SKU / product_id 能力掩码 |
| board | `include/board.h` | `src/board.c` | new | **SoC** 外设 `board_config_t` + mask；片外器件不进此表 |
| button | `include/button.h` | `src/button.c` | ESP/STM32 button | 业务按键；表来自 `board_button_spec` + `flexible_button` |
| input | `include/input.h` | `src/input.c` | STM32 `tmp/input` | 板级 IRQ：`board_irq` + GPIO 双边沿 → `EVT_ID_INPUT` → `input_schedule` |
| led_scene | `include/led_scene.h` | `src/led_scene.c` | STM32 `tmp/led_scene` | LED GPIO 来自 `board_config`（无 LED mask 则跳过） |

**Not in common**: serial `cmd` — factory-only；片外 EPD/NFC 等 — 各工程 `source/`。

## Board 配置（结构体 + 掩码）

- SoC 射频：`BOARD_MASK_BLE` / `BOARD_MASK_NFC`（片上 NFCT，**不是** SPI 从机）
- 片外模组（如 EPD）→ `projects/<name>/source/`，挂 SPI/I2C
- 对照：[`demo`](../projects/demo)（UART×1 LED×2）vs [`eink_tag`](../projects/eink_tag)（UART×1 SPI从机×1 EPD + BLE + NFCT）
- SPI：**总线** `spi_bus_count` 与 **从机** `spi_slave_count`（CS）分开计数；中断线用 `irq_count`
- 按键：`button_init()` + 周期 `button_schedule()`（默认 20ms）
- 中断输入：`input_init()` 吃 `board_irq_*`；主循环 `input_schedule()`（去抖后回调）

```c
board_init();
button_init(button_log_notify);
input_init(input_log_notify);
/* loop: button_schedule(); input_schedule(); */
```

## Not migrated yet

OTA/boot_slot、buzzer、battery — 板级 / B3 再做。

## Usage in an app

```cmake
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
target_sources(app PRIVATE board/board_config.c ...)
```

```c
#include "log.h"
#include "fw_version.h"
#include "nvs.h"
#include "device_profile.h"
#include "board.h"

log_init(NULL);
nvs_init();
board_init();
LOG_INFO("ver %s product=%s board=%s", version_get_string(),
         device_profile_product()->name, board_config_get()->name);
```

`prj.conf` 需启用 Settings（demo/factory 已加）：

```
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_NVS=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_NVS=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y
```

产品固件设 `CONFIG_NRF54_NVS_DEFAULT_DEVICE_ID`（如 demo=`0x53530110`）；厂测保持 `0x0` 读 NVS `did`。

Do **not** include `zephyr/logging/log.h` in the same file as `log.h` (macro clash).

**Version**: set `CONFIG_APP_VERSION_STRING="x.y.z"` in `prj.conf`, or let `nrf.cmd release <x.y.z>` write `prj_version.conf` (gitignored).

**Release**: `nrf.cmd release` merges `prj_release.conf` → `CONFIG_APP_BUILD_RELEASE=y`, so `log_output()` in `src/log.c` is a no-op. App code may still call `LOG_*`; nothing is emitted.
