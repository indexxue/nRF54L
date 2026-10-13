/**
 * @file zigbee_app.c
 * @brief Zigbee ED 薄封装：Basic+Identify 最小端点；回调只入队。
 *
 * 注意：本文件使用 Zephyr LOG，勿 include common/log.h。
 */

#include "zigbee_app.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zboss_api.h>
#include <zb_nrf_platform.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>

#include "zb_range_extender.h"

LOG_MODULE_REGISTER(zigbee_app, LOG_LEVEL_INF);

#define APP_ZIGBEE_ENDPOINT 10
#define TEMPLATE_INIT_BASIC_POWER_SOURCE ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE

struct zb_device_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
};

static struct zb_device_ctx s_dev_ctx;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(identify_attr_list, &s_dev_ctx.identify_attr.identify_time);

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(basic_attr_list, &s_dev_ctx.basic_attr.zcl_version,
				 &s_dev_ctx.basic_attr.power_source);

ZB_DECLARE_RANGE_EXTENDER_CLUSTER_LIST(app_template_clusters, basic_attr_list, identify_attr_list);

ZB_DECLARE_RANGE_EXTENDER_EP(app_template_ep, APP_ZIGBEE_ENDPOINT, app_template_clusters);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(app_template_ctx, app_template_ep);

K_MSGQ_DEFINE(s_zb_evt_q, sizeof(zigbee_app_evt_t), 8, 4);

static void zigbee_app_evt_post(zigbee_app_state_t state, bool with_addr)
{
	zigbee_app_evt_t evt = {
		.state = state,
		.nwk_addr = 0U,
		.ieee_valid = false,
	};

	if (with_addr && ZB_JOINED()) {
		zb_ieee_addr_t ieee;

		evt.nwk_addr = zb_get_short_address();
		zb_get_long_address(ieee);
		memcpy(evt.ieee, ieee, sizeof(evt.ieee));
		evt.ieee_valid = true;
	}

	if (k_msgq_put(&s_zb_evt_q, &evt, K_NO_WAIT) != 0) {
		LOG_WRN("zigbee evt queue full, drop state=%d", (int)state);
	}
}

static void zigbee_app_clusters_attr_init(void)
{
	s_dev_ctx.basic_attr.zcl_version = ZB_ZCL_VERSION;
	s_dev_ctx.basic_attr.power_source = TEMPLATE_INIT_BASIC_POWER_SOURCE;
	s_dev_ctx.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

static void zigbee_app_do_leave(zb_bufid_t bufid)
{
	ZVUNUSED(bufid);
	LOG_INF("zigbee: leave / factory reset network");
	zb_bdb_reset_via_local_action(0);
}

status_t zigbee_app_start(void)
{
	LOG_INF("zigbee: start ED channel=%d", CONFIG_ZIGBEE_CHANNEL);

	ZB_AF_REGISTER_DEVICE_CTX(&app_template_ctx);
	zigbee_app_clusters_attr_init();
	zigbee_enable();
	zigbee_app_evt_post(ZIGBEE_APP_STATE_IDLE, false);
	return STATUS_OK;
}

void zigbee_app_steer_request(void)
{
	LOG_INF("zigbee: steer request (user_input_indicate)");
	zigbee_app_evt_post(ZIGBEE_APP_STATE_STEERING, false);
	user_input_indicate();
}

void zigbee_app_leave_request(void)
{
	ZB_SCHEDULE_APP_CALLBACK(zigbee_app_do_leave, 0);
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

	switch (sig) {
	case ZB_BDB_SIGNAL_DEVICE_FIRST_START:
	case ZB_BDB_SIGNAL_DEVICE_REBOOT:
		if (status == RET_OK && ZB_JOINED()) {
			zigbee_app_evt_post(ZIGBEE_APP_STATE_JOINED, true);
		} else if (status != RET_OK) {
			zigbee_app_evt_post(ZIGBEE_APP_STATE_FAILED, false);
		}
		break;

	case ZB_BDB_SIGNAL_STEERING:
		if (status == RET_OK && ZB_JOINED()) {
			zigbee_app_evt_post(ZIGBEE_APP_STATE_JOINED, true);
		} else {
			zigbee_app_evt_post(ZIGBEE_APP_STATE_FAILED, false);
		}
		break;

	case ZB_ZDO_SIGNAL_LEAVE:
		zigbee_app_evt_post(ZIGBEE_APP_STATE_IDLE, false);
		break;

	default:
		break;
	}

	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	if (bufid) {
		zb_buf_free(bufid);
	}
}
