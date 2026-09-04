/**
 * @file epd.h
 * @brief 工程侧墨水屏胶水：片外器件，底层走 board SPI + 本文件 GPIO。
 */

#ifndef EINK_TAG_EPD_H
#define EINK_TAG_EPD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/** 配置 CS/DC/RST/BUSY GPIO；SPI 取自 board_spi_spec(0)。尚未接 cbb/epd_2in9。 */
status_t epd_init(void);

#ifdef __cplusplus
}
#endif

#endif /* EINK_TAG_EPD_H */
