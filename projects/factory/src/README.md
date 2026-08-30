# src/

本工程**应用生命周期**（Zephyr RTOS）。外设 / 仅厂测驱动仍在 `../lib/`。

| 文件 | 职责 |
|------|------|
| `main.c` | 极薄入口：`app_startup()` → `app_run()` |
| `app.h` / `app.c` | 初始化顺序 + 主线程运行循环 |
| （可选）其它 `.c` | 仅编排用的胶水；大块实现进 `lib/<module>/` |

## 启动顺序（约定）

1. `log_init` / `log_set_level`
2. `event_init`
3. 本工程 `lib/*` 硬件 init（没有则跳过）
4. factory：`lib/cmd` → `cmd_init` / `cmd_register_defaults`（**不在** `common/`）
5. 按需 `K_THREAD_DEFINE` / `k_thread_create`
6. `app_run`：主线程业务或 idle/心跳（正常不返回）

Zephyr 下**不必**再启动调度器；`main` 本身已是线程。
