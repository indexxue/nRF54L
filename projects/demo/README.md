# demo

工程模板 + **TY rev_a 冒烟**（UART + `led_scene` bootup + 内部 LFCLK RC）。

| 功能 | 引脚 |
|------|------|
| UART TX / RX | P1.09 / P1.10（115200） |
| LED0 / LED1 | P1.04 / P1.05（默认 ACTIVE_LOW） |
| 时钟 | `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC` |

板级：[`board/app.overlay`](board/app.overlay)（pinmux）+ [`board/board_config.c`](board/board_config.c)（`board_config_t` 掩码/规格表）。共享参考：[`boards/ty_rev_a.overlay`](../../boards/ty_rev_a.overlay)。

```powershell
.\nrf.cmd -Project demo build
.\nrf.cmd -Project demo flash
# 串口工具接 TX=P1.09 RX=P1.10 GND；应见 heartbeat；LED 跑 BOOTUP 场景
```

Board 目标名仍为 `nrf54l15dk/nrf54l15/cpuapp`（见 `nrf.env`）。

## 必含文件

| 文件 | 作用 |
|------|------|
| `CMakeLists.txt` | `EXTRA_ZEPHYR_MODULES`（common + start）+ `DTC_OVERLAY_FILE` |
| `prj.conf` | UART / GPIO / RC 时钟 / Settings |
| `board/board_config.c` | 本工程 `board_config_get()` |
| `board/app.overlay` | 本工程 pinmux |
| `src/app.c` | `app_init` / `app_run`（平台 `main` 在 `start/`） |
| `lib/` | 本工程私有库（空占位） |

不要提交 `build/`。
