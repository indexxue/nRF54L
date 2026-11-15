/**
 * @file epd_board.h
 * @brief ink_panel：Waveshare 4.2" B V2（400×300 BWR）板级胶水
 */

#ifndef INK_PANEL_EPD_BOARD_H
#define INK_PANEL_EPD_BOARD_H

#include "type.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 配置 SPI/GPIO，上电并 init 面板；成功返回 STATUS_OK */
status_t epd_board_bringup(void);

/** 后台线程跑一次黑白红 demo 全刷（可阻塞该线程数十秒） */
void epd_board_smoke_start(void);

bool epd_board_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_EPD_BOARD_H */
