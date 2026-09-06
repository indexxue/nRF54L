/**
 * @file ble_evt.h
 * @brief BLE 协议层可选回调（GAP 状态）；业务 CMD 走 common/event + ble_cmd_fetch。
 */

#ifndef NRF54_BLE_EVT_H
#define NRF54_BLE_EVT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum {
	BLE_EVT_STACK_READY = 0,
	BLE_EVT_ADV_STARTED,
	BLE_EVT_ADV_STOPPED,
	BLE_EVT_CONNECTED,
	BLE_EVT_DISCONNECTED,
} ble_evt_id_t;

typedef struct {
	ble_evt_id_t id;
	uint8_t reason;
} ble_evt_t;

typedef void (*ble_evt_cb_t)(const ble_evt_t *evt, void *user);

void ble_evt_set_callback(ble_evt_cb_t cb, void *user);
void ble_evt_post(ble_evt_id_t id, uint8_t reason);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_EVT_H */
