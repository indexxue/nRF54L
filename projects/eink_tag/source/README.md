# source/

片外器件胶水。

| 文件 | 状态 |
|------|------|
| `epd.c` / `epd_board.h` | 已接 **cbb/epd** GDEY029Z95（2.9" BWR） |

SPI 从机名 `"epd"` 在 `board/board_config.c`；DC/RST/BUSY 在 `board/app.overlay` 的 `zephyr,user`。
