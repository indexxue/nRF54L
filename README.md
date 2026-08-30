# nRF54L15 固件工程

基于 **nRF54L15** 的量产向固件仓库：**nRF Connect SDK（Zephyr）**，不是 STM32 CubeMX/Keil，也不是 TI TM4C 那套 CCS 流程。

**P0 已验收**（目录 + SDK 路径 + `build`）。**烧录 / 上板验证暂时跳过**（见 [`doc/plan.md`](doc/plan.md)）。

| 项 | 说明 |
|----|------|
| 芯片 | nRF54L15（应用核 Cortex-M33） |
| 硬件 | **自有 PCB**（主目标）；仓库暂用 DK 板名编译，板级文件见 `boards/` |
| SDK | nRF Connect SDK **3.2.x**（本地，默认 `D:\ncs\v3.2.0`） |
| 首工程 | [`projects/demo`](projects/demo) — `common` 日志 / 命令骨架 |
| 烧录 | **SWD** → J-Link（命令保留；**本阶段不强制实测**） |
| 串口 | 日志/命令用；默认不烧录 |
| 骨架阶段不做 / 暂缓 | 上板 flash 验证、MCUBoot / OTA、BLE、WSL、`nrf monitor` |

## 和 STM32 / TM4C 的差别（避免套错习惯）

| | STM32 / 部分 TM4C | 本仓库（nRF54L15） |
|--|-------------------|-------------------|
| IDE/SDK | CubeMX + HAL + Keil 等 | **NCS + west + Zephyr** |
| 入口 | `build.cmd` / UV4 | **`nrf.cmd`** → `west` |
| 下载 | ST-Link SWD / 部分 UART ISP | **J-Link SWD**（主路径） |
| 串口 | 常兼调试/ISP | **打印 + `cmd`**，不是默认烧录口 |
| 板级 | `.ioc` / 引脚表 | **Devicetree / overlay**（`boards/`） |

自有板调试座（**以后**验证时再用）：SWDIO、SWCLK、GND、VTref；日志另接 UART。

## 目录

```
nRF54L15/
├── projects/          # 多应用工程（demo、后续量产工程…）
├── boards/            # 自有板 Zephyr board / overlay（待补）
├── common/            # 多工程共享源码（无 Zephyr 封装）
├── modules/           # Zephyr module 封装（如 nrf54_common）
├── cbb/               # Git 子模块：器件驱动（indexxue/cbb）
├── tools/             # 主机侧工具
├── firmware/          # 发布归档：见 firmware/README.md（命名约定 B）
├── scripts/           # setup_env / verify_env
├── doc/               # 开发说明
├── nrf.cmd / nrf.ps1  # 统一命令入口
└── nrf.env.example    # 复制为 nrf.env：NCS_ROOT / BOARD / PROJECT
```

## 首次环境

1. 安装 [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util)；J-Link 可稍后（本阶段可不烧录）。
2. C 盘紧时把 SDK 装到 **D:**（脚本默认 `D:\ncs`）。
3. 运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\setup_env.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\verify_env.ps1
git submodule update --init --recursive
.\nrf.cmd detect
.\nrf.cmd build
```

4. 确认 `nrf.env` 中 `NCS_ROOT` / `NCS_VERSION` / `BOARD`；自有板 DTS 就绪后再改 `BOARD=`。

SDK 约 **10–20 GB**，**不进 Git**。

## 日常命令

| 命令 | 功能 |
|------|------|
| `.\nrf.cmd detect` | 检查路径 / 工具 |
| `.\nrf.cmd build` | 调试编译（有 log） |
| `.\nrf.cmd -Project factory release 0.0.1` | 发布（`log.c` 空输出）并归档 elf/hex/bin |
| `.\nrf.cmd -Project demo flash` | J-Link SWD 烧录（**本阶段可跳过**） |
| `.\nrf.cmd fullclean` | 删除该工程 `build/` |

`-Project` 对应 `projects/<名>/`。

## 新增工程

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\new_project.ps1 -Name myapp
.\nrf.cmd -Project myapp build
```

手工步骤与「放哪里」见 [`doc/getting_started.md`](doc/getting_started.md)。共享：`common/`；工程私有驱动：`projects/<name>/lib/<module>/`；Zephyr 接入：[`modules/nrf54_common`](modules/nrf54_common)。

已有共享模块：`log` / `version` / `flexible_button` / `crc32` / `event`。厂测串口 `cmd` 在 `projects/factory/lib/cmd/`（非 common）。

## 相关文档

- [`firmware/README.md`](firmware/README.md) — 发布包命名（`chip_project_ver_date_sign.hex`）
- [`doc/plan.md`](doc/plan.md) — **当前计划**（P0 已验收；下一步业务工程 / 板级）
- [`doc/getting_started.md`](doc/getting_started.md) — 环境、开新工程清单、（以后）上板
- [`doc/decisions.md`](doc/decisions.md) — 骨架决策
- [`boards/README.md`](boards/README.md) — 自定义板怎么加
