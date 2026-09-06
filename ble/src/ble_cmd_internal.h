/**
 * @file ble_cmd_internal.h
 * @brief CMD 线程与 ingress 共用的 raw 帧队列（模块内）。
 */

#ifndef NRF54_BLE_CMD_INTERNAL_H
#define NRF54_BLE_CMD_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ble_cmd.h"
#include "type.h"

typedef struct {
	uint16_t len;
	uint8_t data[BLE_CMD_FRAME_MAX];
} ble_cmd_raw_t;

status_t ble_cmd_raw_put(const uint8_t *data, size_t len);
bool ble_cmd_raw_get(ble_cmd_raw_t *out, int32_t timeout_ms);

status_t ble_cmd_parsed_put(const ble_cmd_msg_t *msg);
bool ble_cmd_parsed_get(ble_cmd_msg_t *out);

status_t ble_cmd_parse(const uint8_t *data, size_t len, ble_cmd_msg_t *out);

#endif /* NRF54_BLE_CMD_INTERNAL_H */
