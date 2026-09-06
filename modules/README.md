# modules/

Zephyr / NCS **module wrappers**（各含 `zephyr/module.yml`）。

| Module | Path | Sources |
|--------|------|---------|
| `nrf54_common` | `nrf54_common/` | [`../../common`](../common) |
| `nrf54_start` | `nrf54_start/` | [`../../start`](../start) |
| `nrf54_ble` | `nrf54_ble/` | [`../../ble`](../ble) |

App CMake（`find_package(Zephyr)` 之前）：

```cmake
list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_ble   # 需要 BLE 时
)
```
