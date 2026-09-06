/**
 * @file ble_gap.c
 * @brief 可连接广播 + 连接回调（断线后自动再广播）。
 */

#include "ble_gap.h"

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include "ble_evt.h"
#include "log.h"

#define DEVICE_NAME     CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

static atomic_t s_advertising;
static atomic_t s_connected;
static atomic_t s_cnt_conn;
static atomic_t s_cnt_disc;

static const struct bt_data s_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

status_t ble_gap_start_adv(void)
{
	int err;

	err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, s_ad, ARRAY_SIZE(s_ad), NULL, 0);
	if (err != 0) {
		LOG_ERROR("ble_gap: adv_start %d", err);
		atomic_set(&s_advertising, 0);
		return STATUS_FAIL;
	}

	atomic_set(&s_advertising, 1);
	ble_evt_post(BLE_EVT_ADV_STARTED, 0U);
	LOG_INFO("ble_gap: advertising as \"%s\"", DEVICE_NAME);
	return STATUS_OK;
}

status_t ble_gap_stop_adv(void)
{
	int err;

	if (atomic_get(&s_advertising) == 0) {
		return STATUS_OK;
	}

	err = bt_le_adv_stop();
	if ((err != 0) && (err != -EALREADY)) {
		LOG_WARN("ble_gap: adv_stop %d", err);
		return STATUS_FAIL;
	}

	atomic_set(&s_advertising, 0);
	ble_evt_post(BLE_EVT_ADV_STOPPED, 0U);
	LOG_INFO("ble_gap: advertising stopped");
	return STATUS_OK;
}

static void connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0U) {
		LOG_WARN("ble_gap: connect fail %s err=0x%02x", addr, err);
		return;
	}

	atomic_set(&s_connected, 1);
	atomic_set(&s_advertising, 0);
	atomic_inc(&s_cnt_conn);
	ble_evt_post(BLE_EVT_CONNECTED, 0U);
	ble_evt_post(BLE_EVT_ADV_STOPPED, 0U);
	LOG_INFO("ble_gap: connected %s", addr);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	atomic_set(&s_connected, 0);
	atomic_inc(&s_cnt_disc);
	ble_evt_post(BLE_EVT_DISCONNECTED, reason);
	LOG_INFO("ble_gap: disconnected %s reason=0x%02x", addr, reason);
}

static void recycled(void)
{
	(void)ble_gap_start_adv();
}

BT_CONN_CB_DEFINE(ble_gap_conn_cbs) = {
	.connected = connected,
	.disconnected = disconnected,
	.recycled = recycled,
};

bool ble_gap_is_advertising(void)
{
	return atomic_get(&s_advertising) != 0;
}

bool ble_gap_is_connected(void)
{
	return atomic_get(&s_connected) != 0;
}

uint32_t ble_gap_count_connected(void)
{
	return (uint32_t)atomic_get(&s_cnt_conn);
}

uint32_t ble_gap_count_disconnected(void)
{
	return (uint32_t)atomic_get(&s_cnt_disc);
}
