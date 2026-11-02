# source/

片外器件胶水（协议栈在仓库根 `ble/`，经 `modules/nrf54_ble` 接入）。

| 路径 | 状态 |
|------|------|
| `epd.c` / `epd_board.h` | **cbb/epd** GDEY029Z95 |
| PN5180 Reader | FW smoke + **BTN2 持续扫卡**（ISO15693）；IRQ/BLE CMD 未做 |

蓝牙：`#include "ble.h"` / `"ble_cmd.h"`，见仓库 [`ble/README.md`](../../../ble/README.md)。
