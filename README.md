# nRF54L

基于 **Nordic nRF54L15** 的量产向固件仓库，使用 **nRF Connect SDK（Zephyr）** 构建。支持多应用工程、共享 `common` 层，以及器件驱动子模块 [`cbb`](https://github.com/indexxue/cbb)。

| | |
|--|--|
| 芯片 | nRF54L15（应用核 Cortex-M33） |
| SDK | nRF Connect SDK **3.2.x** |
| 构建入口 | `nrf.cmd` → `west` |
| 烧录 | J-Link **SWD** |
| 远程 | https://github.com/indexxue/nRF54L |

## 特性

- 多工程布局：`projects/demo`（模板）、`projects/eink_tag`（NFC/BLE→墨水屏标签）、`projects/factory`（厂测骨架）、`projects/zigbee_ed`（Zigbee ED，原 `zigbee_demo`）、`projects/zigbee_router`（Zigbee Router + 控灯）、`projects/ink_panel`（EVT 墨水屏终端 · BLE；[`终态`](tmp/ink_panel/product.html) / [`阶段`](tmp/ink_panel/stage.html)）
- 共享起机：`start/`（`main` / `start_boot` / `start_poll`）；工程只写 `app_init` / `app_run`
- 共享模块：`log` / `nvs` / `device_profile` / `board`（结构体+掩码）/ `led_scene` / `event` / `crc32` / `flexible_button` / `fw_version`
- 分区规划已锁定：MCUBoot 双槽 + storage（见 [`doc/flash_layout.md`](doc/flash_layout.md)）
- 统一命令：`build` / `release` / `flash` / `detect` / `fullclean`
- 器件驱动：Git 子模块 `cbb/`（按需接入，不绑死 MCU）

## 仓库结构

```
nRF54L15/
├── projects/           # 应用工程（demo、factory、…）
├── bootloader/         # 共享 MCUBoot 配置（sysbuild 子镜像，非独立工程）
├── start/              # 公共 main / start_boot / start_poll
├── common/             # 多工程共享 C 源码
├── ble/                # BLE 协议层（CMD 线程 / GAP / GATT）
├── modules/            # Zephyr module → common、start、ble
├── boards/             # 自有板 Devicetree / overlay（待补）
├── cbb/                # 子模块：通用器件驱动
├── firmware/           # release 归档（二进制默认不入库）
├── scripts/            # setup_env / verify_env / new_project
├── tools/              # 主机侧工具
├── doc/                # 计划、决策、分区、上手指南
├── nrf.cmd / nrf.ps1   # 统一 CLI
└── nrf.env.example     # 复制为 nrf.env（本机路径，不入库）
```

| 代码放哪里 | 路径 |
|------------|------|
| 公共起机 / `main` | `start/`（`modules/nrf54_start`） |
| MCUBoot 共享配置 | `bootloader/`（各 APP sysbuild 引用） |
| 多工程复用库 | `common/` |
| BLE 协议 / 标定 CMD | `ble/`（`modules/nrf54_ble`） |
| 仅某一工程 | `projects/<name>/src` + `lib/<module>/` |
| 板级引脚 / DTS | `boards/` |
| 器件驱动（跨 MCU） | `cbb/<chip>/` |

## 环境要求

- Windows 10/11
- [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util)（`sdk-manager`）
- 本地 NCS（默认 `D:\ncs\v3.2.0`，约 10–20 GB，**不进 Git**）
- 烧录阶段：SEGGER J-Link

## 快速开始

```powershell
git clone --recurse-submodules git@github.com:indexxue/nRF54L.git
cd nRF54L   # 或本地目录名 nRF54L15

# 若克隆时未带子模块：
git submodule update --init --recursive

copy nrf.env.example nrf.env
# 按需编辑 NCS_ROOT / NCS_VERSION / BOARD / PROJECT

powershell -ExecutionPolicy Bypass -File .\scripts\setup_env.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\verify_env.ps1

.\nrf.cmd detect
.\nrf.cmd build
```

默认板目标：`nrf54l15dk/nrf54l15/cpuapp`（自有板 DTS 就绪后改 `nrf.env` 中 `BOARD=`）。

## 常用命令

| 命令 | 说明 |
|------|------|
| `.\nrf.cmd detect` | 检查 SDK / 工具链 / 工程 |
| `.\nrf.cmd build` | 调试编译（默认只编 app domain，快） |
| `.\nrf.cmd build-all` | 全量 sysbuild（MCUBoot + app + merged.hex） |
| `.\nrf.cmd -Project factory build` | 编译指定工程（app domain） |
| `.\nrf.cmd -Project zigbee_ed build` | Zigbee ED 联调（需 `NCS_ZIGBEE_ROOT`，见该工程 README） |
| `.\nrf.cmd -Project zigbee_router build` | Zigbee Router + OnOff 控灯联调 |
| `.\nrf.cmd -Project factory release 0.0.1` | 发布构建并归档到 `firmware/` |
| `.\nrf.cmd -Project demo flash` | J-Link：MCUBoot + APP@slot0 |
| `.\nrf.cmd -Project factory flash` | J-Link：厂测 signed.bin **只写 slot1**（不动 boot/slot0） |
| `.\nrf.cmd boot-mode factory\|app` | 写 `boot_share` 旁路标志 |
| `.\nrf.cmd fullclean` | 删除当前工程 `build/` |

新增工程：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\new_project.ps1 -Name myapp
.\nrf.cmd -Project myapp build
```

## 烧录指令（日常 / 厂测 / 量产）

仓库根目录执行。板目标见 `nrf.env` 的 `BOARD=`。详细步骤也可看 [`projects/demo/README.md`](projects/demo/README.md)、[`projects/factory/README.md`](projects/factory/README.md)。

### 1. 平时项目调试烧录（量产 APP @ slot0）

改业务代码、看串口心跳时用。默认工程可把 `demo` 换成 `eink_tag` 等。

```powershell
.\nrf.cmd -Project demo build          # 快编：只编 app
.\nrf.cmd -Project demo flash          # 编 app → merged.hex → SWD（MCUBoot + image-0）

# 改过 bootloader / sysbuild / pm_static 后：
.\nrf.cmd -Project demo fullclean
.\nrf.cmd -Project demo build-all
.\nrf.cmd -Project demo flash
```

期望串口：`demo printk ok ... image-0` / `demo heartbeat`。

### 2. 厂测调试烧录（factory @ slot1 旁路）

联调 **image-0 = 量产 APP、image-1 = factory**：先烧 slot0，再单独写 slot1，用 `boot-mode` 进厂测。

```powershell
# A. 量产 APP → slot0（含 MCUBoot）
.\nrf.cmd -Project demo build-all
.\nrf.cmd -Project demo flash

# B. 厂测镜像 → slot1（不动 boot / slot0）
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash

# C. 下次启动走 slot1
.\nrf.cmd boot-mode factory
# 复位 / 断电 → Boot: factory→secondary；串口见 factory

# 出厂测（不擦 slot1）：串口 ftmexit，或 P0.00 连击 5→4→3→2
# 或工具：.\nrf.cmd boot-mode app 后再复位
```

仅改厂测代码、slot0 已是合法 APP 时：

```powershell
.\nrf.cmd -Project factory build
.\nrf.cmd -Project factory flash
.\nrf.cmd boot-mode factory
```

`factory flash` **只写 slot1**，不会覆盖 slot0。要装 Boot + 量产 APP，用对应工程的 `flash`（如 `demo flash`）。

### 3. 量产烧录

发布产物进 `firmware/<x.y.z>/`（关调试 log）；产线用归档 hex / 或开发机等价 SWD。

```powershell
# 打量产包（APP）
.\nrf.cmd -Project demo release 0.0.1
# → firmware/0.0.1/nrf54l15_demo_0.0.1_YYYYMMDD_unsigned.{hex,bin,elf}

# 打厂测包（需要时；产线可另烧 slot1）
.\nrf.cmd -Project factory release 0.0.1
```

产线最小 SWD 路径（开发机等价）：

```powershell
.\nrf.cmd -Project demo release 0.0.1
.\nrf.cmd -Project demo flash                 # Boot + APP@slot0
.\nrf.cmd -Project factory release 0.0.1
.\nrf.cmd -Project factory flash                 # 厂测@slot1（可选，按产线是否预置厂测）
.\nrf.cmd boot-mode app                       # 出厂默认走 slot0
```

归档命名见 [`firmware/README.md`](firmware/README.md)。OTA 才擦/覆盖 slot1；出厂测（`ftmexit` / 按键）**只清 `boot_mode`，不擦 slot1**。

## Flash 分区（摘要）

量产目标布局（应用核 1428 KB RRAM；FLPR 96 KB 预留）：

| 分区 | 大小 | 用途 |
|------|------|------|
| mcuboot | 64 KB | Bootloader |
| image-0 | 664 KB | 量产 APP（如 demo） |
| image-1 | 664 KB | OTA 暂存 ∪ 厂测 |
| settings_storage | 32 KB | Settings / NVS |
| boot_share | 4 KB | Boot ↔ APP 旁路（`boot_mode` 等） |

`storage` 合计仍为 36 KB（Settings 32 + 末页 `boot_share` 4）。**`demo` / `factory` 已启用 MCUBoot**（双槽 + `merged.hex`）；`eink_tag` 仍为预 MCUBoot 单镜像。共享配置见 [`bootloader/`](bootloader/)；约定见 [`doc/flash_layout.md`](doc/flash_layout.md)。

## 与 STM32 习惯的差异

| | STM32（本 monorepo 其它工程） | 本仓库 |
|--|------------------------------|--------|
| 工具链 | CubeMX + HAL + Keil | NCS + west + Zephyr |
| 入口 | `build.cmd` / UV4 | `nrf.cmd` |
| 下载 | ST-Link / UART ISP | J-Link SWD |
| 板级 | `.ioc` | Devicetree / overlay |

## 文档

| 文档 | 内容 |
|------|------|
| [`doc/getting_started.md`](doc/getting_started.md) | 环境细节、开新工程清单、上板接线 |
| [`doc/flash_layout.md`](doc/flash_layout.md) | RRAM 分区与写权限 |
| [`doc/plan.md`](doc/plan.md) | 阶段计划与验收状态 |
| [`doc/decisions.md`](doc/decisions.md) | 架构决策记录 |
| [`doc/zephyr_eink_tag_deep_dive.md`](doc/zephyr_eink_tag_deep_dive.md) | 面向 `eink_tag` 的 Zephyr 深挖路线 |
| [`firmware/README.md`](firmware/README.md) | 发布包命名约定 |
| [`boards/README.md`](boards/README.md) | 自定义板接入 |
| [`common/README.md`](common/README.md) | 共享模块说明 |
| [`cbb/README.md`](cbb/README.md) | 器件驱动子模块 |

## 当前状态

- 骨架与 `build` 已可用；共享层含 NVS / device_profile
- 上板烧录冒烟、MCUBoot/OTA、BLE、自有板 DTS：按计划推进（见 [`doc/plan.md`](doc/plan.md)）
