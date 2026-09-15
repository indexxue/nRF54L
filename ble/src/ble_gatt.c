/**
 * @file ble_gatt.c
 * @brief GATT RX Write → ble_cmd_ingress；TX Notify ← ble_gatt_notify。
 *
 * Service/RX/TX 128-bit UUID 前 32 bit = FNV-1a(sn)；须在 bt_enable 前 prepare。
 */

#include "ble_gatt.h"

#include <errno.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/sys/atomic.h>

#include "ble_cmd.h"
#include "log.h"
#include "nvs.h"

#define BLE_UUID_SHORT_SVC 0x0001U
#define BLE_UUID_SHORT_RX 0x0002U
#define BLE_UUID_SHORT_TX 0x0003U
/** hash 为 0 时回退（与历史 eink_tag product_id 一致） */
#define BLE_UUID_PREFIX_FALLBACK 0x53530120U

static struct bt_uuid_128 s_svc_uuid;
static struct bt_uuid_128 s_rx_uuid;
static struct bt_uuid_128 s_tx_uuid;

static struct bt_conn *s_conn;
static atomic_t s_notify_enabled;
static bool s_uuid_ready;

/** FNV-1a 32 — 与 tools/nrf_console sn→UUID 算法一致 */
static uint32_t ble_gatt_sn_hash32(const char *sn)
{
	uint32_t h = 2166136261U;

	if (sn == NULL) {
		return BLE_UUID_PREFIX_FALLBACK;
	}

	for (; *sn != '\0'; sn++) {
		h ^= (uint32_t)(uint8_t)*sn;
		h *= 16777619U;
	}

	return (h != 0U) ? h : BLE_UUID_PREFIX_FALLBACK;
}

static void ble_gatt_set_uuid128(struct bt_uuid_128 *out, uint32_t prefix, uint16_t short_id)
{
	*out = (struct bt_uuid_128)BT_UUID_INIT_128(
		BT_UUID_128_ENCODE(prefix, short_id, 0x1000, 0x8000, 0x00805f9b34fb));
}

status_t ble_gatt_prepare(void)
{
	char sn[NVS_SN_SIZE];
	uint32_t prefix;

	(void)nvs_sn_get(sn);
	prefix = ble_gatt_sn_hash32(sn);

	ble_gatt_set_uuid128(&s_svc_uuid, prefix, BLE_UUID_SHORT_SVC);
	ble_gatt_set_uuid128(&s_rx_uuid, prefix, BLE_UUID_SHORT_RX);
	ble_gatt_set_uuid128(&s_tx_uuid, prefix, BLE_UUID_SHORT_TX);
	s_uuid_ready = true;

	LOG_INFO("ble_gatt: sn=%s uuid=%08x-0001/0002/0003-1000-8000-00805f9b34fb", sn,
		 (unsigned)prefix);
	return STATUS_OK;
}

static ssize_t ble_gatt_rx_write(struct bt_conn *conn, const struct bt_gatt_attr *attr,
				 const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
	ARG_UNUSED(conn);
	ARG_UNUSED(attr);
	ARG_UNUSED(flags);

	if (offset != 0U) {
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}

	if (ble_cmd_ingress(buf, len) != STATUS_OK) {
		return BT_GATT_ERR(BT_ATT_ERR_INSUFFICIENT_RESOURCES);
	}

	return (ssize_t)len;
}

static void ble_gatt_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
	ARG_UNUSED(attr);
	atomic_set(&s_notify_enabled, (value == BT_GATT_CCC_NOTIFY) ? 1 : 0);
	LOG_INFO("ble_gatt: CCC notify=%d", (int)atomic_get(&s_notify_enabled));
}

BT_GATT_SERVICE_DEFINE(
	ble_eink_svc, BT_GATT_PRIMARY_SERVICE(&s_svc_uuid),
	BT_GATT_CHARACTERISTIC(&s_rx_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
			       BT_GATT_PERM_WRITE, NULL, ble_gatt_rx_write, NULL),
	BT_GATT_CHARACTERISTIC(&s_tx_uuid.uuid, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE, NULL, NULL,
			       NULL),
	BT_GATT_CCC(ble_gatt_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

/* attrs: [0]svc [1]rx decl [2]rx val [3]tx decl [4]tx val [5]ccc */
#define BLE_GATT_TX_VALUE_ATTR (&ble_eink_svc.attrs[4])

static void ble_gatt_connected(struct bt_conn *conn, uint8_t err)
{
	if (err != 0U) {
		return;
	}

	if (s_conn != NULL) {
		bt_conn_unref(s_conn);
		s_conn = NULL;
	}
	s_conn = bt_conn_ref(conn);
}

static void ble_gatt_disconnected(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(reason);

	if (s_conn == conn) {
		bt_conn_unref(s_conn);
		s_conn = NULL;
	}
	atomic_set(&s_notify_enabled, 0);
}

BT_CONN_CB_DEFINE(ble_gatt_conn_cbs) = {
	.connected = ble_gatt_connected,
	.disconnected = ble_gatt_disconnected,
};

status_t ble_gatt_init(void)
{
	if (!s_uuid_ready) {
		LOG_WARN("ble_gatt: prepare skipped — filling UUID now");
		(void)ble_gatt_prepare();
	}
	LOG_INFO("ble_gatt: RX write + TX notify");
	return STATUS_OK;
}

bool ble_gatt_notify_ready(void)
{
	return (s_conn != NULL) && (atomic_get(&s_notify_enabled) != 0);
}

status_t ble_gatt_notify(const void *data, size_t len)
{
	int err;

	if ((data == NULL) || (len == 0U) || (len > BLE_CMD_FRAME_MAX)) {
		return STATUS_INVALID_ARG;
	}

	if (!ble_gatt_notify_ready()) {
		return STATUS_INVALID_STATE;
	}

	err = bt_gatt_notify(s_conn, BLE_GATT_TX_VALUE_ATTR, data, len);
	if (err != 0) {
		LOG_WARN("ble_gatt: notify failed %d", err);
		return STATUS_FAIL;
	}

	return STATUS_OK;
}
