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

- 多工程布局：`projects/demo`（模板）、`projects/eink_tag`（NFC/BLE→墨水屏标签）、`projects/factory`（厂测骨架）
- 共享模块：`log` / `nvs` / `device_profile` / `board`（结构体+掩码）/ `led_scene` / `event` / `crc32` / `flexible_button` / `fw_version`
- 分区规划已锁定：MCUBoot 双槽 + storage（见 [`doc/flash_layout.md`](doc/flash_layout.md)）
- 统一命令：`build` / `release` / `flash` / `detect` / `fullclean`
- 器件驱动：Git 子模块 `cbb/`（按需接入，不绑死 MCU）

## 仓库结构

```
nRF54L15/
├── projects/           # 应用工程（demo、factory、…）
├── common/             # 多工程共享 C 源码
├── modules/nrf54_common/  # Zephyr module → 引用 common
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
| 多工程复用 | `common/` |
| 仅某一工程 | `projects/<name>/lib/<module>/` |
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
| `.\nrf.cmd build` | 调试编译（默认 `PROJECT`，通常为 `demo`） |
| `.\nrf.cmd -Project factory build` | 编译指定工程 |
| `.\nrf.cmd -Project factory release 0.0.1` | 发布构建并归档到 `firmware/` |
| `.\nrf.cmd -Project demo flash` | J-Link SWD 烧录 |
| `.\nrf.cmd fullclean` | 删除当前工程 `build/` |

新增工程：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\new_project.ps1 -Name myapp
.\nrf.cmd -Project myapp build
```

## Flash 分区（摘要）

量产目标布局（应用核 1428 KB RRAM；FLPR 96 KB 预留）：

| 分区 | 大小 | 用途 |
|------|------|------|
| mcuboot | 64 KB | Bootloader |
| image-0 | 664 KB | 量产 APP |
| image-1 | 664 KB | OTA 暂存 ∪ 厂测 |
| storage | 36 KB | Settings / NVS（含 `boot_mode`） |

当前构建为**预 MCUBoot** 单镜像（`pm_static.yml`：`app` + `settings_storage`）。完整双槽表与写权限约定见 [`doc/flash_layout.md`](doc/flash_layout.md)。

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
| [`firmware/README.md`](firmware/README.md) | 发布包命名约定 |
| [`boards/README.md`](boards/README.md) | 自定义板接入 |
| [`common/README.md`](common/README.md) | 共享模块说明 |
| [`cbb/README.md`](cbb/README.md) | 器件驱动子模块 |

## 当前状态

- 骨架与 `build` 已可用；共享层含 NVS / device_profile
- 上板烧录冒烟、MCUBoot/OTA、BLE、自有板 DTS：按计划推进（见 [`doc/plan.md`](doc/plan.md)）
