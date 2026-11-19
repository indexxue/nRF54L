/**
 * @file epd_board.h
 * @brief ink_panel：WS 4.2" B V2 板级胶水（DT GPIO + nPM1300；demo 与 eink_tag 共用）
 */

#ifndef INK_PANEL_EPD_BOARD_H
#define INK_PANEL_EPD_BOARD_H

#include "type.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 硬件在位扫描（无 MISO）：上电 + RST/SWRESET 后看 BUSY。
 * PRESENT → STATUS_OK；ABSENT → STATUS_FAIL。
 */
status_t epd_board_hw_scan(void);

/** 配置 GPIO、上电、init 面板（调用线程可能阻塞数秒） */
status_t epd_board_bringup(void);

/**
 * 与 eink_tag 同款 BWR demo 全刷（cbb epd_display_demo_bwr_fill）。
 * 阻塞调用线程；勿在 main 调。
 */
status_t epd_board_test_run(void);

/**
 * 后台线程：hw_scan → bringup → **一次** demo 全刷（对齐 eink_tag 上电行为）。
 * 刷屏只堵本线程；main/灯效仍跑。
 */
void epd_board_smoke_start(void);

bool epd_board_is_ready(void);
bool epd_board_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_EPD_BOARD_H */
