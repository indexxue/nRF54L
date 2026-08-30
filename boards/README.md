# boards/

自有 PCB 的 Zephyr/NCS **board 定义**（dts、Kconfig、overlay）放这里。

## 当前

工程默认仍编译官方目标名（能编通骨架）：

`nrf54l15dk/nrf54l15/cpuapp`

这 **不是** 说硬件必须是 Nordic DK。自有板在未提供 DTS 前，只能借用该 SoC 的官方板配置；**GPIO/UART/晶振等可能与实物不符**，烧录靠外接 J-Link SWD，日志靠你板子上的 UART。

## 自有板就绪后

1. 在本目录增加 board（或 `BOARD_ROOT` 指向的树）：chip、时钟、console UART、SWD 相关等
2. `nrf.env` 中设置 `BOARD=<你的板目标>`
3. 在 `doc/` 写清 SWD 座与 UART 引脚
4. `.\nrf.cmd fullclean` 后再 `build` / `flash`

参考上游：NCS 内 `zephyr/boards/nordic/nrf54l15dk/`。
