/**
 * @file nfc_board.h
 * @brief 片上 NFCT 进场检测冒烟（Type 2 Tag 仿真，非外挂 NFC）。
 */

#ifndef EINK_TAG_NFC_BOARD_H
#define EINK_TAG_NFC_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

/** 启动 T2T 仿真并开始侦测场；进/出场打日志，LED_B 点亮表示在场内。 */
status_t nfc_board_init(void);

bool nfc_board_is_ready(void);

/** T2T 回调认为场内（FIELD_ON 后、FIELD_OFF 前）。 */
bool nfc_board_field_present(void);

/**
 * 主循环调用：轮询 FIELDPRESENT 寄存器；有变化即打印。
 * 适合 iPhone 场极短时补抓。
 */
void nfc_board_poll(void);

/** FIELDPRESENT 原始值（bit0=场存在，bit1=锁存）。 */
uint8_t nfc_board_field_status_raw(void);

uint32_t nfc_board_count_field_on(void);
uint32_t nfc_board_count_field_off(void);
uint32_t nfc_board_count_data_read(void);

#ifdef __cplusplus
}
#endif

#endif /* EINK_TAG_NFC_BOARD_H */
