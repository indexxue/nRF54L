# source/

片外器件胶水。

| 文件 | 状态 |
|------|------|
| `epd.c` / `epd.h` | 保留；**当前未编入**（板未接 EPD/SPI） |

接上墨水屏后：在 `board` 开 SPI 从机，并在 `CMakeLists.txt` 加回 `source/epd.c`。
