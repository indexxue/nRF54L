# remote/ — FLPR (RISC-V) image

本目录是 **独立的 Zephyr 应用**，目标板为 `…/cpuflpr`，**不是** Cortex-M33 上的 `src/app.c`。

| | 应用核 `cpuapp` | 本目录 FLPR `cpuflpr` |
|--|----------------|----------------------|
| ISA | Arm Cortex-M33 | RISC-V (VPR) |
| 入口 | 仓库 `start/` → `app_init`/`app_run` | 本目录 `src/main.c` |
| 共享库 | `common/` / `start/` | **不链接**（保持小而独立） |
| 控制台 | uart20（本工程 overlay） | uart30（板级默认） |
| 存放 | RRAM 应用区 | RRAM `0x165000` 起 96 KB，运行时拷到 SRAM |

由上层 [`../sysbuild.cmake`](../sysbuild.cmake) 编进同一次 `west build`；烧录时随 `merged` 镜像一起下。
