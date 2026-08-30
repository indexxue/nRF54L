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
| device_profile | `include/device_profile.h` | `src/device_profile.c` | ESP profile | demo/factory 空档案；按键表预留 |

**Not in common**: serial `cmd` — factory-only at `projects/factory/lib/cmd/`.

## Not migrated yet

OTA/boot_slot、board 引脚表、led_scene、buzzer、battery、业务层 `button` — 板级 / B3 再做。

## Usage in an app

```cmake
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
```

```c
#include "log.h"
#include "fw_version.h"
#include "nvs.h"
#include "device_profile.h"

log_init(NULL);
nvs_init();
LOG_INFO("ver %s product=%s", version_get_string(), device_profile_product()->name);
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
