/**
 * @file nfc_board.h
 * @brief ink_panel：片内 NFCT Type 2 Tag 产测
 */

#ifndef INK_PANEL_NFC_BOARD_H
#define INK_PANEL_NFC_BOARD_H

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

/** setup T2T + 固定 NDEF Text + emulation_start；成功返回 STATUS_OK */
status_t nfc_board_bringup(void);

/** 主循环调用：消费 pending，串口打 field/data 日志 */
void nfc_board_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_NFC_BOARD_H */
