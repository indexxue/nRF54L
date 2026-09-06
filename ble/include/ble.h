/**
 * @file ble.h
 * @brief 仓库级 BLE 协议层入口（与 common 同级；app 勿直接 include zephyr/bluetooth）。
 */

#ifndef NRF54_BLE_H
#define NRF54_BLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

/**
 * Bring-up: CMD thread → stack → gatt → gap adv.
 * Requires BOARD_MASK_BLE.
 */
status_t ble_init(void);

/**
 * Tear-down for fail paths: stop advertising.
 * CMD thread stays (idempotent restart not required on reboot-oriented fail).
 */
status_t ble_stop(void);

bool ble_is_ready(void);
bool ble_is_advertising(void);
bool ble_is_connected(void);

uint32_t ble_count_connected(void);
uint32_t ble_count_disconnected(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_H */
