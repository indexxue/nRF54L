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

bool ble_gap_is_advertising(void);
bool ble_gap_is_connected(void);

uint32_t ble_gap_count_connected(void);
uint32_t ble_gap_count_disconnected(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_GAP_H */
