/**
 * @file ble_gap.c
 * @brief 可连接广播 + 连接回调；断线是否重广由 restart 开关控制。
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
static atomic_t s_adv_slow;
/** 默认 1：断线后 recycled 再广播（兼容常开广播产品）。 */
static atomic_t s_restart_on_disc = ATOMIC_INIT(1);

static const struct bt_data s_ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

status_t ble_gap_start_adv(void)
{
	int err;
	bool slow = atomic_get(&s_adv_slow) != 0;

	if (atomic_get(&s_connected) != 0) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_advertising) != 0) {
		return STATUS_OK;
	}

	if (slow) {
		err = bt_le_adv_start(BT_LE_ADV_PARAM(BT_LE_ADV_OPT_CONN, BT_GAP_ADV_SLOW_INT_MIN,
						      BT_GAP_ADV_SLOW_INT_MAX, NULL),
				      s_ad, ARRAY_SIZE(s_ad), NULL, 0);
	} else {
		err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_2, s_ad, ARRAY_SIZE(s_ad), NULL, 0);
	}
	if ((err != 0) && (err != -EALREADY)) {
		LOG_ERROR("ble_gap: adv_start %d", err);
		atomic_set(&s_advertising, 0);
		return STATUS_FAIL;
	}

	atomic_set(&s_advertising, 1);
	ble_evt_post(BLE_EVT_ADV_STARTED, 0U);
	LOG_INFO("ble_gap: advertising as \"%s\" (%s)", DEVICE_NAME, slow ? "slow" : "fast");
	return STATUS_OK;
}

void ble_gap_set_restart_on_disconnect(bool enable)
{
	atomic_set(&s_restart_on_disc, enable ? 1 : 0);
	LOG_INFO("ble_gap: restart_on_disconnect=%d", enable ? 1 : 0);
}

bool ble_gap_get_restart_on_disconnect(void)
{
	return atomic_get(&s_restart_on_disc) != 0;
}

status_t ble_gap_set_adv_slow(bool slow)
{
	bool was = atomic_get(&s_adv_slow) != 0;

	atomic_set(&s_adv_slow, slow ? 1 : 0);

	if (was == slow) {
		return STATUS_OK;
	}

	if (atomic_get(&s_connected) != 0) {
		LOG_INFO("ble_gap: adv mode → %s (deferred, connected)", slow ? "slow" : "fast");
		return STATUS_OK;
	}

	if (atomic_get(&s_advertising) != 0) {
		(void)ble_gap_stop_adv();
		return ble_gap_start_adv();
	}

	LOG_INFO("ble_gap: adv mode → %s (idle)", slow ? "slow" : "fast");
	return STATUS_OK;
}

bool ble_gap_is_adv_slow(void)
{
	return atomic_get(&s_adv_slow) != 0;
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
	if (atomic_get(&s_restart_on_disc) == 0) {
		LOG_INFO("ble_gap: recycled — no auto adv (button-wake mode)");
		return;
	}
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
