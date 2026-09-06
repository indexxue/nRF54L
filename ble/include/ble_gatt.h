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

status_t ble_gatt_init(void);

/** Notify raw CMD frame on TX characteristic (needs link + CCC notify). */
status_t ble_gatt_notify(const void *data, size_t len);

bool ble_gatt_notify_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_GATT_H */
