# Getting started

> **P0 已验收**：SDK 路径 + 工程目录 + `build` 通过（见 [`plan.md`](plan.md)）。  
> **烧录 / 上板验证暂时跳过**。下文「接线 / flash」供以后使用。

## 前置（本阶段）

- Windows 10/11
- [nRF Util](https://www.nordicsemi.com/Products/Development-tools/nRF-Util)（`sdk-manager`）
- 本地 NCS：默认 **`D:\ncs\v3.2.0`**（`setup_env` 安装）

可选（烧录阶段再装）：SEGGER J-Link、自有板 SWD / UART。

## SDK 路径（重点）

| 路径 | 含义 |
|------|------|
| `D:\ncs` | `sdk-manager` 的 `--install-dir`（toolchain、downloads） |
| `D:\ncs\v3.2.0` | `NCS_ROOT`（`zephyr/`、`nrf/` 等） |
| `D:\ncs\toolchains\` | 配套编译器 |
| 仓库内 `nrf.env` | 本机覆盖：`NCS_ROOT` / `NCS_VERSION` / `BOARD` / `PROJECT`（不进 Git） |

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\setup_env.ps1 -NcsVersion v3.2.0 -InstallRoot D:\ncs
powershell -ExecutionPolicy Bypass -File .\scripts\verify_env.ps1
.\nrf.cmd detect
.\nrf.cmd build
```

`nrf.cmd` 用 `nrfutil … toolchain launch --chdir <NCS_ROOT> -- west …`，不把 SDK 拷进仓库。

## 工程目录（重点）

```
projects/<name>/     # 每个应用一个目录（demo 为模板）
  src/               # app_init/app_run（平台 main 在 start/）
  lib/               # 本工程私有库（默认空；见 lib/README.md）
common/              # 多工程共享 C
modules/nrf54_common/# Zephyr module，编译时引用 common
boards/              # 自有板 DTS（待补）
tools/ firmware/ scripts/ doc/
```

## 新增工程（清单）

**推荐**（从 demo 复制并改名）：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\new_project.ps1 -Name myapp
.\nrf.cmd -Project myapp build
```

**手工等价步骤**：

1. 复制 `projects/demo` → `projects/<name>`（不要拷 `build/`）
2. `CMakeLists.txt`：`project(<name>)`；保留 `EXTRA_ZEPHYR_MODULES` → `../../modules/nrf54_common`
3. 改 `src/main.c` / `README.md` 中的工程名
4. `.\nrf.cmd -Project <name> build`
5. 可选：`nrf.env` 里 `PROJECT=<name>`

**放哪里**：

| 内容 | 目录 |
|------|------|
| 入口 / 生命周期 | `start/`（`main` / `start_boot`）+ `projects/<name>/src/app.c`（`app_init` → `app_run`） |
| **仅本工程**用的驱动/业务 | `projects/<name>/lib/<module>/`（目录名=源文件名；勿先塞 `common/`） |
| 多工程复用的 C | `common/`（经 `modules/nrf54_common` 接入） |
| 自有板 DTS / overlay | `boards/`（有原理图后再做） |

私有库细则与 CMake 写法见各工程 `lib/README.md`。CMake **显式列出**，不加 glob。

## 以后：接线与烧录（暂缓）

日常下载用 **SWD**（非串口 ISP）：

| 调试器 | 板端 |
|--------|------|
| SWDIO / SWCLK / GND / VTref | 对应焊盘 |
| RESET（可选） | nRESET |

```powershell
.\nrf.cmd flash
```

日志用自备串口工具。板目标策略见 [`../boards/README.md`](../boards/README.md)。
