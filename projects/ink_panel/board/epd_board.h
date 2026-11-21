/**
 * @file epd_board.h
 * @brief ink_panel EPD：SPIM20 + nPM1300；erase + BWR demo
 */

#ifndef INK_PANEL_EPD_BOARD_H
#define INK_PANEL_EPD_BOARD_H

#include "type.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

status_t epd_board_hw_scan(void);
status_t epd_board_bringup(void);

/** 与 eink_tag 相同：先擦白再 BWR demo。阻塞调用线程；勿在 main 调。 */
status_t epd_board_test_run(void);

/** 仅擦白（兼容） */
status_t epd_board_erase_run(void);

/** 后台：bringup → 上电 erase+demo；之后收 BTN 重刷请求 */
void epd_board_smoke_start(void);

/** 入队再跑 erase+demo（K_NO_WAIT） */
status_t epd_board_demo_request(void);

/** 同 demo_request（按键用） */
status_t epd_board_erase_request(void);

bool epd_board_is_ready(void);
bool epd_board_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_EPD_BOARD_H */
