/**
 * @file pn5180_board.h
 * @brief 工程侧 PN5180 胶水：board SPI22 + DT GPIO → cbb/pn5180。
 *
 * 冒烟：init+FW。持续扫卡：专用线程 + start/stop（勿在 GATT/ISR 调）。
 */

#ifndef EINK_TAG_PN5180_BOARD_H
#define EINK_TAG_PN5180_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "type.h"

/**
 * 绑定 spi22/"pn5180" + RST/BUSY，pn5180_init（含 FW 校验），串口打版本。
 * 可阻塞数百 ms～数秒；失败返回 STATUS_FAIL（不拖死后续 BLE/EPD）。
 */
status_t pn5180_board_smoke(void);

/**
 * 创建扫卡线程（须 smoke 成功后调一次）。
 * 之后用 scan_start / scan_stop；入队 K_NO_WAIT。
 */
status_t pn5180_board_scan_init(void);

/**
 * 请求开始持续 ISO15693 Inventory（开 RF，周期扫）。
 * @return STATUS_OK 已入队；STATUS_INVALID_STATE 未 ready/已在扫；STATUS_FAIL 队列满。
 */
status_t pn5180_board_scan_start(void);

/**
 * 请求停止扫卡（线程侧 rf_off）。
 * @return STATUS_OK 已入队；STATUS_INVALID_STATE 未在扫；STATUS_FAIL 队列满。
 */
status_t pn5180_board_scan_stop(void);

bool pn5180_board_is_ready(void);
bool pn5180_board_scan_is_active(void);

#ifdef __cplusplus
}
#endif

#endif /* EINK_TAG_PN5180_BOARD_H */
