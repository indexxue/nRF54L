/**
 * @file ble_gatt.h
 * @brief GATT：RX Write → ingress；TX Notify ← egress。
 */

#ifndef NRF54_BLE_GATT_H
#define NRF54_BLE_GATT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

/**
 * 按当前 NVS sn 填充 Service/RX/TX UUID（FNV-1a → 前 32 bit）。
 * 必须在 bt_enable / ble_stack_init 之前调用。
 */
status_t ble_gatt_prepare(void);

status_t ble_gatt_init(void);

/** 主动断开当前连接（无连接返回 INVALID_STATE）。 */
status_t ble_gatt_disconnect(void);

/** Notify raw CMD frame on TX characteristic (needs link + CCC notify). */
status_t ble_gatt_notify(const void *data, size_t len);

bool ble_gatt_notify_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_GATT_H */
