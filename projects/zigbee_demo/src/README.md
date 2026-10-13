# src/

本工程**业务逻辑**。平台起机在仓库 [`start/`](../../../start/)（`main` / `start_boot` / `start_poll`）。

| 文件 | 职责 |
|------|------|
| `app.c` | `app_init()` + `app_run()`；循环里调 `start_poll()`（含公共 FTM 跳槽） |
| （可选）其它 `.c` | 仅编排用胶水；大块实现进 `lib/<module>/` |

FLPR（RISC-V）固件不在本目录，而在同级 [`../remote/`](../remote/)。

## 启动顺序

1. `start_boot`（log / event / nvs / board / 可选 HFXO / mask 外设）
2. `app_init` — 本工程业务 init（含提示 FLPR 已由 nordic-flpr 拉起）
3. `app_run` — 主循环（正常不返回）
4. 并行：`remote/` 在 FLPR 核上跑自己的 `main`（uart30）

Zephyr 下**不必**再启动调度器；`main` 本身已是线程。
