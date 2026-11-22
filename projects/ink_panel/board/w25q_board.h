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

/**
 * @brief Phase 1 冒烟：GPIO + SPIM22 + w25q_init，RTT 打印 JEDEC/size。
 * @return 0 成功，负值失败（不阻断后续 EPD）
 */
int w25q_board_smoke(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_W25Q_BOARD_H */
