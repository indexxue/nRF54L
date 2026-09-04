# boards/

自有 PCB 的 Zephyr/NCS **共享参考** overlay / 正式 board 定义放这里。

## 分工

| 路径 | 职责 |
|------|------|
| `boards/ty_rev_a.overlay` | 共享参考 pinmux（文档 / 对照） |
| `projects/<name>/board/app.overlay` | **该工程实际编译**用的 overlay（CMake `DTC_OVERLAY_FILE`） |
| `projects/<name>/board/board_config.c` | 该工程 `board_config_t`（mask + LED/UART/… 规格表） |

引脚只写 overlay；C 侧不重复抄 pin 号。业务模块读 `board_config_get()`。

## 当前 SoC 目标

工程仍编译官方目标名：

`nrf54l15dk/nrf54l15/cpuapp`

### TY rev_a 冒烟引脚（参考）

| 功能 | 引脚 | 说明 |
|------|------|------|
| UART TX | **P1.09** | `uart20` → console / `printk` |
| UART RX | **P1.10** | 115200 8N1 |
| LED0 | **P1.04** | `GPIO_ACTIVE_LOW` |
| LED1 | **P1.05** | 同上 |
| 时钟 | 内部 | LF=`CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC`；HF 无 BLE 时用 HFINT |

## 自有完整 board 就绪后

1. 在本目录增加正式 board（或 `BOARD_ROOT`）
2. `nrf.env` 中设置 `BOARD=<你的板目标>`
3. 把 overlay 内容并入板级 dts；工程 `board_config.c` 仍保留规格表
4. `.\nrf.cmd fullclean` 后再 `build` / `flash`

参考上游：NCS 内 `zephyr/boards/nordic/nrf54l15dk/`。
