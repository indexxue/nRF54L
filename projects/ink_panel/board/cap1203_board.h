/**
 * @file cap1203_board.h
 * @brief ink_panel 板级 CAP1203（与 nPM 共 i2c21）
 */

#ifndef CAP1203_BOARD_H
#define CAP1203_BOARD_H

#include "cap1203.h"

#ifdef __cplusplus
extern "C" {
#endif

extern cap1203_t g_cap1203;

/** init + 使能 CS1/CS2；成功返回 0 */
int cap1203_board_bringup(void);

/** 读触摸掩码；边沿变化时打一行日志并清 INT */
void cap1203_board_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* CAP1203_BOARD_H */
