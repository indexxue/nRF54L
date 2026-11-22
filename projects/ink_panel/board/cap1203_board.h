/**
 * @file cap1203_board.h
 * @brief ink_panel 板级 CAP1203（与 nPM 共 i2c21）
 *
 * CS1 = 触摸 L（翻页）；CS2 = 触摸 R（切模）。
 * ISR 只入队 work；导航边沿经 APP_EVT_TOUCH 交给 app。
 */

#ifndef CAP1203_BOARD_H
#define CAP1203_BOARD_H

#include "cap1203.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CAP1203_NAV_L (1U << 0) /**< CS1 按下边沿 */
#define CAP1203_NAV_R (1U << 1) /**< CS2 按下边沿 */

extern cap1203_t g_cap1203;

/** init + 使能 CS1/CS2；成功返回 0 */
int cap1203_board_bringup(void);

/** 无 IRQ 时的兜底轮询 */
void cap1203_board_poll(void);

/**
 * 取走并清除挂起的导航位（CAP1203_NAV_L / R）。
 * @return 自上次 take 以来累计的按下边沿掩码
 */
uint8_t cap1203_board_nav_take(void);

#ifdef __cplusplus
}
#endif

#endif /* CAP1203_BOARD_H */
