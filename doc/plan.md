# 当前计划（nRF54L15）

更新口径：**烧录与板级硬件验证暂时跳过**；**P0 骨架已验收通过**（目录 + SDK 路径 + `build`）。

## 阶段目标

| 优先级 | 项 | 状态 / 说明 |
|--------|----|-------------|
| P0 | 仓库目录与多工程布局 | **完成**：`projects/` `common/` `modules/` `boards/` `tools/` `firmware/` `scripts/` `doc/` |
| P0 | 本地 NCS 路径可复现 | **完成**：`D:\ncs\v3.2.0` + `nrf.env` / `setup_env` / `verify_env` |
| P0 | 统一命令入口（编为主） | **完成**：`nrf.cmd` → `build` / `fullclean` / `detect`；`flash` 保留但不作为验收 |
| P1 | 工程模板可复制 | **完成**：`projects/demo` + `scripts/new_project.ps1` + 清单见 README / getting_started |
| P1 | SDK / 工具链路径文档化 | **完成**：getting_started + plan 本节路径表 |
| P2 | 自定义 `boards/` | 有原理图后再做 |
| **暂缓** | J-Link SWD 烧录、串口日志硬件冒烟 | **跳过**，不阻塞目录与 SDK 工作 |
| 以后 | MCUBoot/OTA、BLE、接 `cbb` 驱动、`firmware/` 发布流水线 | 板级与业务稳定后 |

## 本阶段要盯住的路径

```
仓库（可进 Git）
  D:\desktopsrc\TY\nRF54L15\
    projects\<name>\     # 应用工程
    common\              # 共享源码
    modules\nrf54_common\  # Zephyr 封装 → 指向 common
    nrf.env              # 本机路径（gitignore）

SDK（本机，不进 Git）
  D:\ncs\                # install-dir（toolchain + downloads）
  D:\ncs\v3.2.0\         # NCS_ROOT（含 zephyr/、nrf/）
  D:\ncs\toolchains\     # 官方工具链

nrf.env 关键键
  NCS_VERSION=v3.2.0
  NCS_ROOT=D:\ncs\v3.2.0
  BOARD=...              # 暂可 DK 名；自有板后改
  PROJECT=demo
  FLASH_RUNNER=jlink     # 保留配置；本阶段不强制实测 flash
```

入口：`nrf.cmd` 通过 `nrfutil sdk-manager toolchain launch --ncs-version … --chdir %NCS_ROOT% -- west …` 使用上述 SDK。

## P0 验收结果（不含烧录）

| # | 项 | 结果 |
|---|----|------|
| 1 | `.\scripts\verify_env.ps1` | **PASS**（NCS_ROOT / zephyr / nrf） |
| 2 | `.\nrf.cmd detect` | **PASS**（路径 / BOARD / 工程列表） |
| 3 | `.\nrf.cmd build` | **PASS**（`projects/demo` → `merged.hex`） |
| 4 | 目录职责 | **清晰**：新应用 → `projects/<name>`；共享 → `common/`；Zephyr 封装 → `modules/` |

验收日：2026-08-30。

## 明确不做（本阶段 / 已收口后仍不做）

- 不把「板上 flash 成功」当里程碑  
- 不要求接好 SWD / 串口验证日志  
- 不展开 OTA / BLE / 自定义 board DTS（除非目录预留说明）

## 应用布局约定（已定）

- `start/`：公共 `main` / `start_boot` / `start_poll`；`src/app.c`：`app_init`/`app_run`；`lib/<module>/`：仅本工程驱动/业务（目录名=源文件名；CMake 显式列出）
- 多工程复用才进 `common/`；详见 `projects/*/src/README.md`、`projects/*/lib/README.md`、[`decisions.md`](decisions.md)

## 下一步（骨架收口之后）

1. **业务 / 厂测逻辑**：在对应工程 `lib/<module>/` 加实现，勿先塞 `common/`  
2. **板级**：原理图/引脚就绪 → `boards/` 自定义板 + 改 `BOARD=`  
3. **烧录冒烟**：SWD 可连后再开 `flash` + 串口（见 [`getting_started.md`](getting_started.md)）

## 烧录验证（以后再开）

恢复条件：自有板 SWD 可连、UART 引脚明确。届时再以 `flash` + 串口工具做冒烟；步骤仍见 [`getting_started.md`](getting_started.md)。
