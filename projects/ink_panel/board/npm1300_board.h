/**
 * @file npm1300_board.h
 * @brief ink_panel 板级 nPM1300：适配 + 产品电源策略
 */

#ifndef NPM1300_BOARD_H
#define NPM1300_BOARD_H

#include <stdbool.h>

#include "npm1300.h"

#ifdef __cplusplus
extern "C" {
#endif

extern npm1300_t g_npm1300;

/** probe + BUCK1=3.0V + 轨默认 + 按 VBUS 配充；成功返回 0 */
int npm1300_board_bringup(void);

/** 读状态并打一行短日志（失败静默） */
void npm1300_board_log_status(void);

/** 1 s 级调用：按 VBUS / inhibit 边沿开闭充电 */
void npm1300_board_poll_charge(void);

/** RAM 禁止充电；true 时即使有 VBUS 也不开充 */
void npm1300_board_charge_inhibit_set(bool inhibit);

/** EPD_PWR_EN → LS2；刷屏窗口开 */
void npm1300_board_epd_power_set(bool on);

/** TOUCH_LDO_EN → LDO1；产品常开 */
void npm1300_board_touch_power_set(bool on);

/**
 * 禁充 → 关充 → 关 EPD → 进 Ship。
 * 成功后不应再访问 I2C；返回 0 表示已发出 ship 任务。
 */
int npm1300_board_enter_ship(void);

#ifdef __cplusplus
}
#endif

#endif /* NPM1300_BOARD_H */
