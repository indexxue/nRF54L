/**
 * @file w25q_board.h
 * @brief ink_panel：W25Q64 SPI NOR（SPIM00 + cbb/w25q）
 */

#ifndef INK_PANEL_W25Q_BOARD_H
#define INK_PANEL_W25Q_BOARD_H

#include "w25q.h"

#ifdef __cplusplus
extern "C" {
#endif

extern w25q_t g_w25q;

/** GPIO + SPIM00 + w25q_init；成功返回 0 */
int w25q_board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_W25Q_BOARD_H */
