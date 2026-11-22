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

/** 主动断开当前连接；无连接返回 INVALID_STATE。 */
status_t ble_disconnect(void);

/** 开始/停止可连接广播（未连接时）。 */
status_t ble_adv_start(void);
status_t ble_adv_stop(void);

/**
 * 断线后是否自动再广播。false=按键唤醒模式（eink_tag）。
 * @see ble_gap_set_restart_on_disconnect
 */
void ble_adv_set_restart_on_disconnect(bool enable);

/** 可选慢广播；按键唤醒产品通常只用快广播窗口。 */
status_t ble_adv_set_slow(bool slow);
bool ble_adv_is_slow(void);

uint32_t ble_count_connected(void);
uint32_t ble_count_disconnected(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_H */
