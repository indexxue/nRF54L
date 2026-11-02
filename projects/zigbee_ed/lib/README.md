# lib/

本工程**私有**驱动 / 业务库。

## 归属

| 代码 | 放哪里 |
|------|--------|
| 多工程都要用 | 仓库根 `common/` 或日后 `zigbee/` + `modules/nrf54_zigbee`（对标 `ble/`） |
| **仅本工程要用** | 本目录 `lib/<模块名>/` |
| 产品编排（按键/灯效/日志） | `src/app.c` |

## 当前模块

```
lib/
└── zigbee/                 # Zigbee ED + OnOff Server（本工程私有）
    ├── zigbee_app.h/.c     # start / steer / leave / evt_fetch；TX 功率 apply
    ├── zigbee_device.h     # 设备/端点声明宏（Basic+Identify+OnOff）
    ├── zb_mem_config_custom.h
    └── zboss_mem_globals.c # ZBOSS RAM 全局（-fno-lto）
```

第二个工程也要复用同一套 Zigbee 胶水时，再上收到仓库根（对齐 `ble/`），**不要**另起 `homebase/` 之类命名。

## CMake

显式列出（不要 glob），见工程根 `CMakeLists.txt`：

```cmake
target_sources(app PRIVATE lib/zigbee/zigbee_app.c lib/zigbee/zboss_mem_globals.c)
target_include_directories(app PRIVATE lib/zigbee)
```
