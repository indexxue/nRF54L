/**
 * @file epd_board.h
 * @brief zigbee_ed：rev_b 上 Waveshare 4.2" B V2（bit-bang）+ 文本刷屏队列
 */

#ifndef ZIGBEE_ED_EPD_BOARD_H
#define ZIGBEE_ED_EPD_BOARD_H

#include "type.h"

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 文本最长（不含 NUL）；超长截断 */
#define EPD_BOARD_TEXT_MAX 96U

/**
 * 启动 EPD 工作线程（init + 可选首屏在线程内）。
 * 可从 app_init 调用；禁止在 Zigbee 回调里刷屏。
 */
void epd_board_start(void);

bool epd_board_is_ready(void);

/**
 * 入队纯文本全刷。深度 1：满则拒（返回 STATUS_NO_MEM）。
 * 任意线程可调；内部 K_NO_WAIT。
 */
status_t epd_board_text_request(const char *text, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* ZIGBEE_ED_EPD_BOARD_H */
