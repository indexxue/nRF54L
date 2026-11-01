/**
 * @file zigbee_app.c
 * @brief Zigbee Router：EP1 Basic+Identify+OnOff Server；回调只入队。
 *
 * 注意：本文件使用 Zephyr LOG，勿 include common/log.h。
 *
 * 入网：仅 STEERING st=0 才算真正入网（含 TCLK）。ZB_JOINED() 可能半入网就为 1，
 * 此时禁止 on_joined；半入网且 commissioning 已结束时先本地 leave 再重试。
 */

#include "zigbee_app.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

#include <zboss_api.h>
#include <zb_nrf_platform.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>

#include "zigbee_device.h"

LOG_MODULE_REGISTER(zigbee_app, LOG_LEVEL_INF);

#define TEMPLATE_INIT_BASIC_POWER_SOURCE ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE
/** BTN1 配网窗：窗内 Steering 失败会重试，超时才 failed */
#define ZIGBEE_APP_STEER_WINDOW_MS 60000
/** leave / 半入网复位后再开 Steering，给栈与对端清表时间 */
#define ZIGBEE_APP_STEER_RETRY_AFTER_LEAVE_MS 3000
#define ZIGBEE_APP_STEER_RETRY_MS 1500

struct zb_device_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
	zb_zcl_on_off_attrs_t on_off_attr;
};

static struct zb_device_ctx s_dev_ctx;
static atomic_t s_steer_window_active;
static atomic_t s_leave_pending;
static struct k_work_delayable s_steer_window_work;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list, &s_dev_ctx.identify_attr.identify_time);

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(basic_attr_list, &s_dev_ctx.basic_attr.zcl_version,
				 &s_dev_ctx.basic_attr.power_source);

ZB_ZCL_DECLARE_ON_OFF_ATTRIB_LIST(on_off_attr_list, &s_dev_ctx.on_off_attr.on_off);

ZB_DECLARE_ON_OFF_LIGHT_CLUSTER_LIST(app_light_clusters, basic_attr_list, identify_attr_list,
				     on_off_attr_list);

ZB_DECLARE_ON_OFF_LIGHT_EP(app_light_ep, ZIGBEE_APP_ENDPOINT, app_light_clusters);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(app_light_ctx, app_light_ep);

K_MSGQ_DEFINE(s_zb_evt_q, sizeof(zigbee_app_evt_t), 8, 4);

static void zigbee_app_evt_post_net(zigbee_app_state_t state, bool with_addr);
static void zigbee_app_do_steer(zb_bufid_t bufid);
static void zigbee_app_do_steer_timeout(zb_bufid_t bufid);
static void zigbee_app_do_leave(zb_bufid_t bufid);
static void zigbee_app_do_force_leave_keep_window(zb_bufid_t bufid);
static void zigbee_app_on_joined(void);
static void zigbee_app_schedule_steer_retry(uint32_t delay_ms);
static void zigbee_app_arm_force_leave_keep_window(void);

static void zigbee_app_steer_window_stop(void)
{
	(void)k_work_cancel_delayable(&s_steer_window_work);
	atomic_set(&s_steer_window_active, 0);
}

/**
 * 仅在 BDB STEERING 成功（或上电已在网）时调用。
 * ZB_JOINED() 可能在 TCLK 完成前就为 true，过早 on_joined 会掐断密钥交换。
 */
static void zigbee_app_on_joined(void)
{
	zigbee_app_steer_window_stop();
	atomic_set(&s_leave_pending, 0);
	(void)ZB_SCHEDULE_APP_ALARM_CANCEL(zigbee_app_do_steer, ZB_ALARM_ANY_PARAM);
	zigbee_network_join_commissioning_set_active(false);
	zigbee_network_rejoin_abort();
	zigbee_app_evt_post_net(ZIGBEE_APP_STATE_JOINED, true);
}

static void zigbee_app_steer_window_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	ZB_SCHEDULE_APP_CALLBACK(zigbee_app_do_steer_timeout, 0);
}

static void zigbee_app_steer_window_start(void)
{
	atomic_set(&s_steer_window_active, 1);
	(void)k_work_reschedule(&s_steer_window_work, K_MSEC(ZIGBEE_APP_STEER_WINDOW_MS));
	LOG_INF("zigbee: steer window %d ms", ZIGBEE_APP_STEER_WINDOW_MS);
}

static void zigbee_app_schedule_steer_retry(uint32_t delay_ms)
{
	if (atomic_get(&s_steer_window_active) == 0) {
		return;
	}
	(void)ZB_SCHEDULE_APP_ALARM_CANCEL(zigbee_app_do_steer, ZB_ALARM_ANY_PARAM);
	ZB_SCHEDULE_APP_ALARM(zigbee_app_do_steer, 0,
			      ZB_MILLISECONDS_TO_BEACON_INTERVAL(delay_ms));
}

/** 先置 leave_pending 并取消已排 steer，避免与 force-leave 回调竞态 */
static void zigbee_app_arm_force_leave_keep_window(void)
{
	atomic_set(&s_leave_pending, 1);
	(void)ZB_SCHEDULE_APP_ALARM_CANCEL(zigbee_app_do_steer, ZB_ALARM_ANY_PARAM);
	ZB_SCHEDULE_APP_CALLBACK(zigbee_app_do_force_leave_keep_window, 0);
}

static void zigbee_app_do_steer_timeout(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);
	if (!atomic_cas(&s_steer_window_active, 1, 0)) {
		return;
	}
	(void)k_work_cancel_delayable(&s_steer_window_work);
	LOG_INF("zigbee: steer window timeout");
	zigbee_network_rejoin_abort();
	zigbee_network_join_commissioning_set_active(false);
	zigbee_app_evt_post_net(ZIGBEE_APP_STATE_FAILED, false);
}

static void zigbee_app_evt_post_net(zigbee_app_state_t state, bool with_addr)
{
	zigbee_app_evt_t evt = {
		.kind = ZIGBEE_APP_EVT_NET,
		.state = state,
		.nwk_addr = 0U,
		.ieee_valid = false,
		.light_on = false,
	};

	if (with_addr && ZB_JOINED()) {
		zb_ieee_addr_t ieee;

		evt.nwk_addr = zb_get_short_address();
		zb_get_long_address(ieee);
		memcpy(evt.ieee, ieee, sizeof(evt.ieee));
		evt.ieee_valid = true;
	}

	if (k_msgq_put(&s_zb_evt_q, &evt, K_NO_WAIT) != 0) {
		LOG_WRN("zigbee evt queue full, drop net state=%d", (int)state);
	}
}

static void zigbee_app_evt_post_light(bool on)
{
	zigbee_app_evt_t evt = {
		.kind = ZIGBEE_APP_EVT_LIGHT,
		.state = ZIGBEE_APP_STATE_IDLE,
		.nwk_addr = 0U,
		.ieee_valid = false,
		.light_on = on,
	};

	if (k_msgq_put(&s_zb_evt_q, &evt, K_NO_WAIT) != 0) {
		LOG_WRN("zigbee evt queue full, drop light=%d", (int)on);
	}
}

static void zigbee_app_clusters_attr_init(void)
{
	s_dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	s_dev_ctx.basic_attr.power_source = TEMPLATE_INIT_BASIC_POWER_SOURCE;
	s_dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
	s_dev_ctx.on_off_attr.on_off = ZB_ZCL_ON_OFF_IS_OFF;
}

static void zigbee_app_do_leave(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);
	LOG_INF("zigbee: leave / factory reset network");
	zigbee_app_steer_window_stop();
	atomic_set(&s_leave_pending, 1);
	(void)ZB_SCHEDULE_APP_ALARM_CANCEL(zigbee_app_do_steer, ZB_ALARM_ANY_PARAM);
	/* 先停自动 rejoin，清网后由 BTN1 显式入网 */
	zigbee_network_rejoin_abort();
	zb_bdb_reset_via_local_action(0);
}

/** 配网窗内半入网：清本地网态，不关窗；LEAVE 后再重试 Steering */
static void zigbee_app_do_force_leave_keep_window(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);
	if (atomic_get(&s_steer_window_active) == 0) {
		atomic_set(&s_leave_pending, 0);
		return;
	}
	atomic_set(&s_leave_pending, 1);
	zigbee_network_rejoin_abort();
	zb_bdb_reset_via_local_action(0);
}

static void zigbee_app_do_nv_erase_reboot(zb_bufid_t bufid)
{
	const struct flash_area *fa = NULL;
	int err;

	ZVUNUSED(bufid);
	LOG_INF("zigbee: erase zboss_nvram + reboot");
	zigbee_app_steer_window_stop();
	atomic_set(&s_leave_pending, 0);
	zigbee_network_rejoin_abort();
	/*
	 * 不用 zb_set_nvram_erase_at_start：Nordic 起栈会强制 FALSE，跨复位无效。
	 * 直接擦 pm 分区 zboss_nvram（ED↔Router 脏网表）。
	 */
	err = flash_area_open(FIXED_PARTITION_ID(zboss_nvram), &fa);
	if (err == 0) {
		err = flash_area_erase(fa, 0, fa->fa_size);
		flash_area_close(fa);
	}
	printk("zigbee: erase zboss_nvram err=%d, rebooting\n", err);
	sys_reboot(SYS_REBOOT_COLD);
}

static void zigbee_app_do_steer(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);
	if (atomic_get(&s_steer_window_active) == 0) {
		return;
	}
	if (atomic_get(&s_leave_pending) != 0) {
		zigbee_app_schedule_steer_retry(ZIGBEE_APP_STEER_RETRY_AFTER_LEAVE_MS);
		return;
	}

	/*
	 * 半入网：NWK 已 joined，但 TCLK/STEERING 未完成。
	 * commissioning 仍活跃 → 正在等 TCLK，勿打断。
	 * commissioning 已结束 → 死等无意义，强制本地 leave 再重试。
	 */
	if (ZB_JOINED()) {
		if (zigbee_network_join_commissioning_active()) {
			return;
		}
		LOG_INF("zigbee: half-joined without STEERING OK — force leave");
		zigbee_app_arm_force_leave_keep_window();
		return;
	}

	zigbee_network_rejoin_abort();
	zigbee_network_join_commissioning_set_active(true);
	if (!bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING)) {
		LOG_WRN("zigbee: steering rejected");
		zigbee_network_join_commissioning_set_active(false);
		zigbee_app_schedule_steer_retry(ZIGBEE_APP_STEER_RETRY_MS);
	}
}

static void zcl_device_cb(zb_bufid_t bufid)
{
	zb_zcl_device_callback_param_t *device_cb_param =
		ZB_BUF_GET_PARAM(bufid, zb_zcl_device_callback_param_t);

	device_cb_param->status = RET_OK;

	switch (device_cb_param->device_cb_id) {
	case ZB_ZCL_SET_ATTR_VALUE_CB_ID: {
		zb_uint16_t cluster_id =
			device_cb_param->cb_param.set_attr_value_param.cluster_id;
		zb_uint16_t attr_id = device_cb_param->cb_param.set_attr_value_param.attr_id;

		if ((cluster_id == ZB_ZCL_CLUSTER_ID_ON_OFF) &&
		    (attr_id == ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID)) {
			bool on = (device_cb_param->cb_param.set_attr_value_param.values.data8 !=
				   0U);

			zigbee_app_evt_post_light(on);
		} else {
			device_cb_param->status = RET_NOT_IMPLEMENTED;
		}
		break;
	}
	default:
		device_cb_param->status = RET_NOT_IMPLEMENTED;
		break;
	}
}

status_t zigbee_app_start(void)
{
	LOG_INF("zigbee: start Router ep=%d mask=0x%08x", ZIGBEE_APP_ENDPOINT,
		CONFIG_ZIGBEE_CHANNEL_MASK);

	ZB_ZCL_REGISTER_DEVICE_CB(zcl_device_cb);
	ZB_AF_REGISTER_DEVICE_CTX(&app_light_ctx);
	zigbee_app_clusters_attr_init();

	/* 属性初值同步进栈（关） */
	ZB_ZCL_SET_ATTRIBUTE(ZIGBEE_APP_ENDPOINT, ZB_ZCL_CLUSTER_ID_ON_OFF,
			     ZB_ZCL_CLUSTER_SERVER_ROLE, ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID,
			     &s_dev_ctx.on_off_attr.on_off, ZB_FALSE);

	zigbee_enable();
	k_work_init_delayable(&s_steer_window_work, zigbee_app_steer_window_work_fn);
	zigbee_app_evt_post_net(ZIGBEE_APP_STATE_IDLE, false);
	return STATUS_OK;
}

void zigbee_app_steer_request(void)
{
	LOG_INF("zigbee: steer request (BTN1)");
	zigbee_app_steer_window_start();
	zigbee_app_evt_post_net(ZIGBEE_APP_STATE_STEERING, false);
	ZB_SCHEDULE_APP_CALLBACK(zigbee_app_do_steer, 0);
}

void zigbee_app_leave_request(void)
{
	ZB_SCHEDULE_APP_CALLBACK(zigbee_app_do_leave, 0);
}

void zigbee_app_nv_erase_reboot_request(void)
{
	ZB_SCHEDULE_APP_CALLBACK(zigbee_app_do_nv_erase_reboot, 0);
}

bool zigbee_app_evt_fetch(zigbee_app_evt_t *out)
{
	if (out == NULL) {
		return false;
	}
	return k_msgq_get(&s_zb_evt_q, out, K_NO_WAIT) == 0;
}

void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *sig_hnd = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hnd);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);
	/* 方案 A：未按键不自动搜网；BTN1 开 60s 窗，窗内失败重试 */
	bool abort_auto_rejoin = false;
	bool retry_steer = false;
	bool force_leave_keep_window = false;
	uint32_t retry_ms = ZIGBEE_APP_STEER_RETRY_MS;

	if (sig != ZB_COMMON_SIGNAL_CAN_SLEEP) {
		printk("zigbee: sig=%u st=%d joined=%d win=%d\n", (unsigned)sig, (int)status,
		       ZB_JOINED() ? 1 : 0, atomic_get(&s_steer_window_active) ? 1 : 0);
	}

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		if (status == RET_OK && ZB_JOINED()) {
			zigbee_app_on_joined();
		} else if (status != RET_OK) {
			zigbee_app_evt_post_net(ZIGBEE_APP_STATE_FAILED, false);
			abort_auto_rejoin = true;
		} else {
			zigbee_app_evt_post_net(ZIGBEE_APP_STATE_IDLE, false);
			abort_auto_rejoin = true;
		}
		break;

	case ZB_BDB_SIGNAL_STEERING:
	case ZB_BDB_SIGNAL_STEERING_CANCELLED:
		/* 唯一认定「配网完成」：STEERING OK（含 TCLK），勿用裸 ZB_JOINED */
		if ((sig == ZB_BDB_SIGNAL_STEERING) && (status == RET_OK) && ZB_JOINED()) {
			zigbee_app_on_joined();
		} else if (atomic_get(&s_steer_window_active) != 0) {
			abort_auto_rejoin = true;
			/*
			 * STEERING 失败但 NWK 仍 joined：半入网。
			 * 不能直接 do_steer（会死等）；先本地 leave。
			 */
			if (ZB_JOINED()) {
				force_leave_keep_window = true;
			} else {
				retry_steer = true;
				retry_ms = ZIGBEE_APP_STEER_RETRY_AFTER_LEAVE_MS;
			}
		} else {
			abort_auto_rejoin = true;
		}
		break;

	case ZB_ZDO_SIGNAL_LEAVE:
		(void)ZB_SCHEDULE_APP_ALARM_CANCEL(zigbee_app_do_steer, ZB_ALARM_ANY_PARAM);
		atomic_set(&s_leave_pending, 0);

		if (atomic_get(&s_steer_window_active) != 0) {
			abort_auto_rejoin = true;
			retry_steer = true;
			retry_ms = ZIGBEE_APP_STEER_RETRY_AFTER_LEAVE_MS;
			LOG_INF("zigbee: leave during steer window — retry");
		} else {
			zigbee_app_steer_window_stop();
			zigbee_app_evt_post_net(ZIGBEE_APP_STATE_IDLE, false);
			abort_auto_rejoin = true;
		}
		break;

	default:
		break;
	}

	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	if (abort_auto_rejoin) {
		zigbee_network_rejoin_abort();
	}
	if (force_leave_keep_window) {
		zigbee_app_arm_force_leave_keep_window();
	} else if (retry_steer && (atomic_get(&s_steer_window_active) != 0) &&
		   (atomic_get(&s_leave_pending) == 0)) {
		zigbee_app_schedule_steer_retry(retry_ms);
	}

	if (bufid) {
		zb_buf_free(bufid);
	}
}
