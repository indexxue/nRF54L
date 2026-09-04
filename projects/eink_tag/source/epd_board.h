/**
 * @file epd_board.h
 * @brief 工程侧墨水屏胶水：board SPI + GPIO → cbb/epd（GDEY029Z95 2.9" BWR）。
 */

#ifndef EINK_TAG_EPD_BOARD_H
#define EINK_TAG_EPD_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "type.h"

/**
 * SPI00 自检：device_ready + 寄存器 + MOSI↔MISO 回环。
 * 须在 epd_board_init() 之前调用；失败则不要测屏。
 */
status_t epd_board_spi_selftest(void);

/** 配置 SPI/CS/DC/RST/BUSY，注册 GDEY029Z95，硬件 init。 */
status_t epd_board_init(void);

/** 黑 → 白 → 红 → 测试图案全刷（阻塞，约数十秒）。 */
status_t epd_board_test_run(void);

bool epd_board_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* EINK_TAG_EPD_BOARD_H */
