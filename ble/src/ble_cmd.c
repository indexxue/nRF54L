/**
 * @file ble_cmd.c
 * @brief 标定包解析 + raw/parsed 双队列。
 */

#include "ble_cmd.h"
#include "ble_cmd_internal.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include "ble_gatt.h"
#include "log.h"

/* Depth 8: covers burst writes while main is blocked (e.g. EPD refresh). */
#define BLE_CMD_RAW_DEPTH    8
#define BLE_CMD_PARSED_DEPTH 8

K_MSGQ_DEFINE(s_raw_q, sizeof(ble_cmd_raw_t), BLE_CMD_RAW_DEPTH, 4);
K_MSGQ_DEFINE(s_parsed_q, sizeof(ble_cmd_msg_t), BLE_CMD_PARSED_DEPTH, 4);

status_t ble_cmd_parse(const uint8_t *data, size_t len, ble_cmd_msg_t *out)
{
	uint16_t cmd_id;
	uint16_t payload_len;

	if ((data == NULL) || (out == NULL) || (len < BLE_CMD_HDR_SIZE)) {
		return STATUS_FAIL;
	}

	cmd_id = sys_get_le16(&data[0]);
	payload_len = sys_get_le16(&data[2]);

	if ((size_t)BLE_CMD_HDR_SIZE + (size_t)payload_len != len) {
		LOG_WARN("ble_cmd: len mismatch hdr_pl=%u frame=%u",
			 (unsigned int)(BLE_CMD_HDR_SIZE + payload_len), (unsigned int)len);
		return STATUS_FAIL;
	}

	if (payload_len > BLE_CMD_PAYLOAD_MAX) {
		LOG_WARN("ble_cmd: payload too big %u", (unsigned int)payload_len);
		return STATUS_FAIL;
	}

	(void)memset(out, 0, sizeof(*out));
	out->cmd_id = cmd_id;
	out->payload_len = payload_len;
	if (payload_len > 0U) {
		(void)memcpy(out->payload, &data[BLE_CMD_HDR_SIZE], payload_len);
	}

	return STATUS_OK;
}

status_t ble_cmd_raw_put(const uint8_t *data, size_t len)
{
	ble_cmd_raw_t raw;

	if ((data == NULL) || (len == 0U) || (len > BLE_CMD_FRAME_MAX)) {
		return STATUS_FAIL;
	}

	(void)memset(&raw, 0, sizeof(raw));
	raw.len = (uint16_t)len;
	(void)memcpy(raw.data, data, len);

	if (k_msgq_put(&s_raw_q, &raw, K_NO_WAIT) != 0) {
		LOG_WARN("ble_cmd: raw queue full");
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

bool ble_cmd_raw_get(ble_cmd_raw_t *out, int32_t timeout_ms)
{
	k_timeout_t t;

	if (out == NULL) {
		return false;
	}

	t = (timeout_ms < 0) ? K_FOREVER : K_MSEC(timeout_ms);
	return k_msgq_get(&s_raw_q, out, t) == 0;
}

status_t ble_cmd_parsed_put(const ble_cmd_msg_t *msg)
{
	if (msg == NULL) {
		return STATUS_FAIL;
	}

	if (k_msgq_put(&s_parsed_q, msg, K_NO_WAIT) != 0) {
		LOG_WARN("ble_cmd: parsed queue full");
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

bool ble_cmd_parsed_get(ble_cmd_msg_t *out)
{
	if (out == NULL) {
		return false;
	}

	return k_msgq_get(&s_parsed_q, out, K_NO_WAIT) == 0;
}

status_t ble_cmd_ingress(const uint8_t *data, size_t len)
{
	return ble_cmd_raw_put(data, len);
}

bool ble_cmd_fetch(ble_cmd_msg_t *out)
{
	return ble_cmd_parsed_get(out);
}

status_t ble_cmd_send(uint16_t cmd_id, const void *payload, uint16_t payload_len)
{
	uint8_t frame[BLE_CMD_FRAME_MAX];
	size_t frame_len;

	if (payload_len > BLE_CMD_PAYLOAD_MAX) {
		return STATUS_INVALID_ARG;
	}
	if ((payload_len > 0U) && (payload == NULL)) {
		return STATUS_INVALID_ARG;
	}

	sys_put_le16(cmd_id, &frame[0]);
	sys_put_le16(payload_len, &frame[2]);
	if (payload_len > 0U) {
		(void)memcpy(&frame[BLE_CMD_HDR_SIZE], payload, payload_len);
	}

	frame_len = (size_t)BLE_CMD_HDR_SIZE + (size_t)payload_len;
	return ble_gatt_notify(frame, frame_len);
}

status_t ble_cmd_send_ack(uint16_t req_cmd_id, status_t status)
{
	uint8_t pl[BLE_CMD_ACK_PAYLOAD_SIZE];

	sys_put_le16(req_cmd_id, &pl[0]);
	sys_put_le32((uint32_t)(int32_t)status, &pl[2]);
	return ble_cmd_send(BLE_CMD_ACK, pl, BLE_CMD_ACK_PAYLOAD_SIZE);
}

status_t ble_cmd_send_heartbeat(uint32_t tick, uint8_t flags)
{
	uint8_t pl[BLE_CMD_HEARTBEAT_PAYLOAD_SIZE];

	sys_put_le32(tick, &pl[0]);
	pl[4] = flags;
	return ble_cmd_send(BLE_CMD_HEARTBEAT, pl, BLE_CMD_HEARTBEAT_PAYLOAD_SIZE);
}
