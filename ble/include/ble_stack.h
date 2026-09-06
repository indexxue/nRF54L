/**
 * @file ble_stack.h
 * @brief SoftDevice / Zephyr BT Host 生命周期。
 */

#ifndef NRF54_BLE_STACK_H
#define NRF54_BLE_STACK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "type.h"

status_t ble_stack_init(void);
bool ble_stack_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_STACK_H */
