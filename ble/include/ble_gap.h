/**
 * @file ble_gap.h
 * @brief GAP：广播 / 连接 / 断线重广。
 */

#ifndef NRF54_BLE_GAP_H
#define NRF54_BLE_GAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

status_t ble_gap_start_adv(void);
status_t ble_gap_stop_adv(void);

/**
 * 断线 recycled 后是否自动再广播。
 * false：按键唤醒产品（断连停广，需按键再 ble_gap_start_adv）。
 * true（默认）：常开可发现。
 */
void ble_gap_set_restart_on_disconnect(bool enable);
bool ble_gap_get_restart_on_disconnect(void);

/**
 * 切换广播间隔：false=FAST_2(100–150ms)，true=SLOW(1–1.2s)。
 * 正在广播时立即 stop+restart；已连接则只记模式，断线重广时生效。
 */
status_t ble_gap_set_adv_slow(bool slow);
bool ble_gap_is_adv_slow(void);

bool ble_gap_is_advertising(void);
bool ble_gap_is_connected(void);

uint32_t ble_gap_count_connected(void);
uint32_t ble_gap_count_disconnected(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_GAP_H */
