# src/

本工程**厂测业务**。平台起机在仓库 [`start/`](../../../start/)。外设 / 仅厂测驱动仍在 `../lib/`。

| 文件 | 职责 |
|------|------|
| `app.c` | `app_init()`（`cmd`）+ `app_run()`；循环里调 `start_poll()` |

## 启动顺序

1. `start_boot`
2. `app_init`：`lib/cmd` → `cmd_init` / `cmd_register_defaults`（**不在** `common/`）
3. `app_run`

Zephyr 下**不必**再启动调度器；`main` 本身已是线程。
