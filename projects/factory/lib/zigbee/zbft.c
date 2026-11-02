/**
 * @file zbft.c
 * @brief Factory Zigbee backdoor R1–M3: TOOL/UUT, allow/join, zbcmd proxy.
 * Callbacks: ingress + schedule only (no sleep / Settings in ZBOSS ctx).
 */

#include "zbft.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include <zboss_api.h>
#include <zb_nrf_platform.h>
#include <zigbee/zigbee_app_utils.h>
#include <zigbee/zigbee_error_handler.h>

#include "cmd.h"
#include "nvs.h"
#include "zb_range_extender.h"

LOG_MODULE_REGISTER(zbft, LOG_LEVEL_INF);

#define ZBFT_EP 10
#define ZBFT_CH_DEFAULT 15U
#define ZBFT_TX_PWR_DEFAULT 8
#define ZBFT_TX_PWR_MIN (-40)
#define ZBFT_TX_PWR_MAX 8
#define ZBFT_NS "zbft"
#define ZBFT_KEY_REQ "req"
#define ZBFT_KEY_CH "ch"
#define ZBFT_BASIC_POWER ZB_ZCL_BASIC_POWER_SOURCE_DC_SOURCE
#define ZBFT_JOIN_SEC_DEFAULT 60U
#define ZBFT_JOIN_SEC_MAX 254U

/* M3 custom ZCL (ping-style overload; not in simple-desc). */
#define ZBFT_CLUSTER_ID 0xBEEFU
#define ZBFT_CMD_REQ 0x00U
#define ZBFT_CMD_RSP 0x01U
#define ZBFT_FCF_REQ 0x11U
#define ZBFT_FCF_RSP 0x19U
#define ZBFT_ZBCMD_MAX 80U
#define ZBFT_ZBCMD_TIMEOUT_MS 3000U

struct zbft_dev_ctx {
	zb_zcl_basic_attrs_t basic_attr;
	zb_zcl_identify_attrs_t identify_attr;
};

static struct zbft_dev_ctx s_dev;
static zbft_mode_t s_mode = ZBFT_MODE_IDLE;
static uint8_t s_channel = ZBFT_CH_DEFAULT;
static int8_t s_tx_power = ZBFT_TX_PWR_DEFAULT;
static bool s_stack_started;
static bool s_net_up;
static bool s_settings_loaded;
static uint8_t s_pending_req = (uint8_t)ZBFT_MODE_IDLE;

/* M2 allow / joined — RAM only (reboot clears). */
static zb_ieee_addr_t s_allow_ieee;
static bool s_allow_ieee_set;
static char s_allow_sn[NVS_SN_SIZE];
static bool s_allow_sn_set;
static zb_ieee_addr_t s_joined_ieee;
static zb_uint16_t s_joined_short;
static bool s_dut_joined;

static zb_ieee_addr_t s_leave_ieee;
static zb_uint16_t s_leave_short;

/* M3 zbcmd proxy state */
static K_SEM_DEFINE(s_zbcmd_sem, 0, 1);
static K_MUTEX_DEFINE(s_zbcmd_lock);
static char s_zbcmd_tx[ZBFT_ZBCMD_MAX];
static char s_zbcmd_rx[ZBFT_ZBCMD_MAX];
static char s_zbcmd_reply[ZBFT_ZBCMD_MAX];
static zb_uint8_t s_zbcmd_seq;
static zb_uint8_t s_zbcmd_expect_seq;
static zb_uint16_t s_zbcmd_peer_short;
static bool s_zbcmd_waiting;
static struct k_work s_zbcmd_work;
static bool s_zbcmd_work_inited;

ZB_ZCL_DECLARE_IDENTIFY_ATTRIB_LIST(zbft_identify_attr_list, &s_dev.identify_attr.identify_time);

ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST(zbft_basic_attr_list, &s_dev.basic_attr.zcl_version,
				 &s_dev.basic_attr.power_source);

ZB_DECLARE_RANGE_EXTENDER_CLUSTER_LIST(zbft_clusters, zbft_basic_attr_list,
					zbft_identify_attr_list);

ZB_DECLARE_RANGE_EXTENDER_EP(zbft_ep, ZBFT_EP, zbft_clusters);

ZBOSS_DECLARE_DEVICE_CTX_1_EP(zbft_device_ctx, zbft_ep);

static int zbft_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next = NULL;
	int rc;

	if (settings_name_steq(name, ZBFT_KEY_REQ, &next) && (next == NULL)) {
		uint8_t v;

		if (len != sizeof(v)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &v, len);
		if (rc >= 0) {
			s_pending_req = v;
		}
		return rc;
	}
	if (settings_name_steq(name, ZBFT_KEY_CH, &next) && (next == NULL)) {
		uint8_t v;

		if (len != sizeof(v)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &v, len);
		if (rc >= 0) {
			if ((v >= 11U) && (v <= 26U)) {
				s_channel = v;
			}
		}
		return rc;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(zbft, ZBFT_NS, NULL, zbft_settings_set, NULL, NULL);

static bool zbft_settings_save(const char *key, const void *data, size_t len)
{
	char path[24];
	int n;
	int rc;

	n = snprintk(path, sizeof(path), "%s/%s", ZBFT_NS, key);
	if ((n < 0) || (n >= (int)sizeof(path))) {
		return false;
	}
	rc = settings_save_one(path, data, len);
	return rc == 0;
}

static void zbft_settings_load(void)
{
	if (s_settings_loaded) {
		return;
	}
	(void)settings_load_subtree(ZBFT_NS);
	s_settings_loaded = true;
}

static int zbft_wipe_zboss_nv(void)
{
	const struct flash_area *fa;
	int err;

	err = flash_area_open(FIXED_PARTITION_ID(zboss_nvram), &fa);
	if (err != 0) {
		printk("zbft: zboss_nvram open fail %d\n", err);
		return err;
	}
	err = flash_area_erase(fa, 0, fa->fa_size);
	flash_area_close(fa);
	if (err != 0) {
		printk("zbft: zboss_nvram erase fail %d\n", err);
	}
	return err;
}

static uint32_t zbft_channel_mask(uint8_t ch)
{
	return (1UL << ch);
}

static void zbft_attr_init(void)
{
	s_dev.basic_attr.zcl_version = ZB_ZCL_VERSION;
	s_dev.basic_attr.power_source = ZBFT_BASIC_POWER;
	s_dev.identify_attr.identify_time = ZB_ZCL_IDENTIFY_IDENTIFY_TIME_DEFAULT_VALUE;
}

static void zbft_allow_session_reset(void)
{
	ZB_IEEE_ADDR_ZERO(s_allow_ieee);
	s_allow_ieee_set = false;
	s_allow_sn[0] = '\0';
	s_allow_sn_set = false;
	ZB_IEEE_ADDR_ZERO(s_joined_ieee);
	s_joined_short = 0;
	s_dut_joined = false;
}

static void zbft_ieee_str(const zb_ieee_addr_t ieee, char *buf, size_t buf_len)
{
	if ((buf == NULL) || (buf_len == 0U)) {
		return;
	}
	if (ieee_addr_to_str(buf, (uint16_t)buf_len, ieee) < 0) {
		(void)snprintk(buf, buf_len, "?");
	}
}

static void zbft_log_ieee(void)
{
	zb_ieee_addr_t ieee;
	char buf[24];

	zb_get_long_address(ieee);
	zbft_ieee_str(ieee, buf, sizeof(buf));
	printk("zbft: ieee=%s\n", buf);
}

static void zbft_leave_cb(zb_uint8_t param, zb_uint16_t user_param)
{
	zb_zdo_mgmt_leave_param_t *req;
	zb_uint8_t tsn;

	(void)user_param;
	req = ZB_BUF_GET_PARAM(param, zb_zdo_mgmt_leave_param_t);
	ZB_IEEE_ADDR_COPY(req->device_address, s_leave_ieee);
	req->dst_addr = s_leave_short;
	req->remove_children = 0;
	req->rejoin = 0;
	tsn = zdo_mgmt_leave_req(param, NULL);
	if (tsn == 0xFFU) {
		zb_buf_free(param);
	}
}

static void zbft_kick_device(const zb_ieee_addr_t ieee, zb_uint16_t short_addr, const char *reason)
{
	char ieee_s[24];
	zb_ret_t ret;

	zbft_ieee_str(ieee, ieee_s, sizeof(ieee_s));
	printk("zbft: join deny ieee=%s reason=%s\n", ieee_s, reason);

	ZB_IEEE_ADDR_COPY(s_leave_ieee, ieee);
	s_leave_short = short_addr;
	ret = zb_buf_get_out_delayed_ext(zbft_leave_cb, 0, 0);
	if (ret != RET_OK) {
		printk("zbft: leave schedule fail %d\n", (int)ret);
	}
}

static void zbft_on_device_annce(zb_zdo_signal_device_annce_params_t *p)
{
	char ieee_s[24];

	if (s_mode != ZBFT_MODE_TOOL) {
		return;
	}

	zbft_ieee_str(p->ieee_addr, ieee_s, sizeof(ieee_s));

	if (!s_allow_ieee_set) {
		zbft_kick_device(p->ieee_addr, p->device_short_addr, "no_allow");
		return;
	}

	if (!ZB_IEEE_ADDR_CMP(p->ieee_addr, s_allow_ieee)) {
		zbft_kick_device(p->ieee_addr, p->device_short_addr, "mismatch");
		return;
	}

	ZB_IEEE_ADDR_COPY(s_joined_ieee, p->ieee_addr);
	s_joined_short = p->device_short_addr;
	s_dut_joined = true;
	printk("zbft: dut joined ieee=%s nwk=0x%04x\n", ieee_s, (unsigned)p->device_short_addr);
}

static void zbft_trim_crlf(char *s)
{
	size_t n;

	if (s == NULL) {
		return;
	}
	n = strlen(s);
	while (n > 0U) {
		char c = s[n - 1U];

		if ((c != '\r') && (c != '\n')) {
			break;
		}
		s[n - 1U] = '\0';
		n--;
	}
}

static void zbft_zbcmd_send_frame(zb_uint8_t param, zb_uint16_t user)
{
	zb_uint8_t cmd_id = (zb_uint8_t)(user & 0xFFU);
	zb_uint8_t fcf = (cmd_id == ZBFT_CMD_RSP) ? ZBFT_FCF_RSP : ZBFT_FCF_REQ;
	const char *payload = (cmd_id == ZBFT_CMD_RSP) ? s_zbcmd_reply : s_zbcmd_tx;
	size_t plen = strlen(payload);
	zb_uint8_t *ptr;
	zb_addr_u dst;
	zb_ret_t ret;

	if (plen >= ZBFT_ZBCMD_MAX) {
		plen = ZBFT_ZBCMD_MAX - 1U;
	}

	ptr = ZB_ZCL_START_PACKET(param);
	*(ptr++) = fcf;
	*(ptr++) = s_zbcmd_seq;
	*(ptr++) = cmd_id;
	if (plen > 0U) {
		ZB_MEMCPY(ptr, payload, plen);
		ptr += plen;
	}

	dst.addr_short = s_zbcmd_peer_short;
	ret = zb_zcl_finish_and_send_packet_new(param, ptr, &dst, ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
						ZBFT_EP, ZBFT_EP, ZB_AF_HA_PROFILE_ID,
						ZBFT_CLUSTER_ID, NULL, 0, ZB_FALSE, 0);
	if (ret != RET_OK) {
		zb_buf_free(param);
		printk("zbft: zbcmd send fail %d\n", (int)ret);
	}
}

static void zbft_zbcmd_work_fn(struct k_work *work)
{
	char out[ZBFT_ZBCMD_MAX];
	size_t out_len = 0U;
	bool rebootish;
	zb_ret_t ret;

	(void)work;
	rebootish = (strncmp(s_zbcmd_rx, "reboot", 6) == 0) ||
		    (strncmp(s_zbcmd_rx, "ftmexit", 7) == 0);

	if (rebootish) {
		(void)snprintk(s_zbcmd_reply, sizeof(s_zbcmd_reply), "%s:now",
			       (s_zbcmd_rx[0] == 'f') ? "ftmexit" : "reboot");
		ret = zb_buf_get_out_delayed_ext(zbft_zbcmd_send_frame, ZBFT_CMD_RSP, 0);
		if (ret != RET_OK) {
			printk("zbft: zbcmd rsp schedule fail\n");
		}
		k_msleep(80);
		(void)cmd_process_line_remote(s_zbcmd_rx, out, sizeof(out), &out_len);
		return;
	}

	(void)cmd_process_line_remote(s_zbcmd_rx, out, sizeof(out), &out_len);
	zbft_trim_crlf(out);
	if (out[0] == '\0') {
		(void)snprintk(out, sizeof(out), "ng");
	}
	(void)strncpy(s_zbcmd_reply, out, sizeof(s_zbcmd_reply) - 1U);
	s_zbcmd_reply[sizeof(s_zbcmd_reply) - 1U] = '\0';

	ret = zb_buf_get_out_delayed_ext(zbft_zbcmd_send_frame, ZBFT_CMD_RSP, 0);
	if (ret != RET_OK) {
		printk("zbft: zbcmd rsp schedule fail\n");
	}
}

static zb_uint8_t zbft_ep_handler(zb_uint8_t param)
{
	zb_zcl_parsed_hdr_t *hdr = ZB_BUF_GET_PARAM(param, zb_zcl_parsed_hdr_t);
	zb_uint8_t *payload;
	zb_uint16_t plen;
	zb_uint16_t src_short;

	if ((hdr->cluster_id != ZBFT_CLUSTER_ID) || (hdr->profile_id != ZB_AF_HA_PROFILE_ID)) {
		return ZB_FALSE;
	}

	if (hdr->addr_data.common_data.source.addr_type != ZB_ZCL_ADDR_TYPE_SHORT) {
		zb_buf_free(param);
		return ZB_TRUE;
	}
	src_short = hdr->addr_data.common_data.source.u.short_addr;
	payload = zb_buf_begin(param);
	plen = zb_buf_len(param);
	if (plen >= ZBFT_ZBCMD_MAX) {
		plen = ZBFT_ZBCMD_MAX - 1U;
	}

	if (hdr->cmd_id == ZBFT_CMD_REQ) {
		if (s_mode != ZBFT_MODE_UUT) {
			zb_buf_free(param);
			return ZB_TRUE;
		}
		ZB_MEMCPY(s_zbcmd_rx, payload, plen);
		s_zbcmd_rx[plen] = '\0';
		s_zbcmd_seq = hdr->seq_number;
		s_zbcmd_peer_short = src_short;
		(void)k_work_submit(&s_zbcmd_work);
		zb_buf_free(param);
		return ZB_TRUE;
	}

	if (hdr->cmd_id == ZBFT_CMD_RSP) {
		if (s_zbcmd_waiting && (hdr->seq_number == s_zbcmd_expect_seq)) {
			ZB_MEMCPY(s_zbcmd_reply, payload, plen);
			s_zbcmd_reply[plen] = '\0';
			zbft_trim_crlf(s_zbcmd_reply);
			s_zbcmd_waiting = false;
			k_sem_give(&s_zbcmd_sem);
		}
		zb_buf_free(param);
		return ZB_TRUE;
	}

	return ZB_FALSE;
}

static void zbft_uut_steer_cb(zb_uint8_t param)
{
	(void)param;
	if ((s_mode != ZBFT_MODE_UUT) || zbft_net_up()) {
		return;
	}
	(void)bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);
}

static void zbft_permit_join_cb(zb_uint8_t param, zb_uint16_t duration)
{
	zb_zdo_mgmt_permit_joining_req_param_t *req;
	zb_uint8_t tsn;

	/* ZBOSS context: BDB steering + explicit permit window. */
	(void)bdb_start_top_level_commissioning(ZB_BDB_NETWORK_STEERING);

	req = ZB_BUF_GET_PARAM(param, zb_zdo_mgmt_permit_joining_req_param_t);
	req->dest_addr = zb_get_short_address();
	req->permit_duration = (zb_uint8_t)duration;
	req->tc_significance = 1;
	tsn = zb_zdo_mgmt_permit_joining_req(param, NULL);
	if (tsn == 0xFFU) {
		zb_buf_free(param);
		printk("zbft: join open fail\n");
		return;
	}
	printk("zbft: join open %u s\n", (unsigned)duration);
}

static void zbft_tx_power_cb(zb_bufid_t param)
{
	zb_tx_power_params_t *p = zb_buf_begin(param);

	if (p->status == RET_OK) {
		printk("zbft: tx_pwr=%d dBm ok\n", (int)p->tx_power);
	} else {
		printk("zbft: tx_pwr set fail st=%d\n", (int)p->status);
	}
	zb_buf_free(param);
}

static void zbft_tx_power_apply_cb(zb_bufid_t param, zb_uint16_t unused)
{
	zb_tx_power_params_t *p;

	(void)unused;
	p = zb_buf_initial_alloc(param, sizeof(*p));
	p->page = ZB_CHANNEL_PAGE0_2_4_GHZ;
	p->channel = s_channel;
	p->tx_power = s_tx_power;
	p->cb = zbft_tx_power_cb;
	ZB_SCHEDULE_APP_CALLBACK(zb_set_tx_power_async, param);
}

static bool zbft_tx_power_apply(void)
{
	zb_ret_t ret;

	if (!s_stack_started) {
		return true;
	}
	ret = zb_buf_get_out_delayed_ext(zbft_tx_power_apply_cb, 0, 0);
	return ret == RET_OK;
}

static void zbft_start_stack(zbft_mode_t mode)
{
	uint32_t mask = zbft_channel_mask(s_channel);

	zbft_allow_session_reset();

	if (!s_zbcmd_work_inited) {
		k_work_init(&s_zbcmd_work, zbft_zbcmd_work_fn);
		s_zbcmd_work_inited = true;
	}

	ZB_AF_REGISTER_DEVICE_CTX(&zbft_device_ctx);
	ZB_AF_SET_ENDPOINT_HANDLER(ZBFT_EP, zbft_ep_handler);
	zbft_attr_init();

	if (mode == ZBFT_MODE_TOOL) {
		zb_set_network_coordinator_role(mask);
		printk("zbft: mode=tool ch=%u pwr=%d\n", (unsigned)s_channel, (int)s_tx_power);
	} else {
		zb_set_network_router_role(mask);
		printk("zbft: mode=uut (router) ch=%u pwr=%d\n", (unsigned)s_channel,
		       (int)s_tx_power);
	}

	zigbee_erase_persistent_storage(ZB_TRUE);
	zigbee_enable();
	s_stack_started = true;
	s_mode = mode;
	s_net_up = false;
	(void)zbft_tx_power_apply();
}

void zboss_signal_handler(zb_bufid_t bufid)
{
	zb_zdo_app_signal_hdr_t *sig_hnd = NULL;
	zb_zdo_app_signal_type_t sig = zb_get_app_signal(bufid, &sig_hnd);
	zb_ret_t status = ZB_GET_APP_SIGNAL_STATUS(bufid);

	if ((sig == ZB_BDB_SIGNAL_FORMATION) && (s_mode == ZBFT_MODE_TOOL) && (status == RET_OK)) {
		/* Formed: do NOT call default (it auto-steers / opens join). */
		s_net_up = true;
		printk("zbft: formed ch=%u pan=0x%04x\n", (unsigned)s_channel,
		       (unsigned)ZB_PIBCACHE_PAN_ID());
		zbft_log_ieee();
		if (bufid) {
			zb_buf_free(bufid);
		}
		return;
	}

	/* UUT: short fixed retry; quiet while searching (only log joined). */
	if ((sig == ZB_BDB_SIGNAL_STEERING) && (s_mode == ZBFT_MODE_UUT)) {
		if ((status == RET_OK) && ZB_JOINED()) {
			s_net_up = true;
			(void)ZB_SCHEDULE_APP_ALARM_CANCEL(zbft_uut_steer_cb, ZB_ALARM_ANY_PARAM);
			printk("zbft: uut joined nwk=0x%04x pan=0x%04x\n",
			       (unsigned)zb_get_short_address(),
			       (unsigned)ZB_PIBCACHE_PAN_ID());
			zbft_log_ieee();
		} else {
			(void)ZB_SCHEDULE_APP_ALARM(zbft_uut_steer_cb, 0, ZB_TIME_ONE_SECOND * 3U);
		}
		if (bufid) {
			zb_buf_free(bufid);
		}
		return;
	}

	if ((sig == ZB_ZDO_SIGNAL_DEVICE_ANNCE) && (s_mode == ZBFT_MODE_TOOL)) {
		zb_zdo_signal_device_annce_params_t *p =
			ZB_ZDO_SIGNAL_GET_PARAMS(sig_hnd, zb_zdo_signal_device_annce_params_t);

		zbft_on_device_annce(p);
	}

	ZB_ERROR_CHECK(zigbee_default_signal_handler(bufid));

	if (bufid) {
		zb_buf_free(bufid);
	}
}

status_t zbft_boot(void)
{
	uint8_t req;
	uint8_t idle = (uint8_t)ZBFT_MODE_IDLE;

	zbft_settings_load();
	req = s_pending_req;

	(void)zbft_wipe_zboss_nv();
	printk("zbft: net wipe boot\n");
	printk("zbft: settings kept\n");

	s_pending_req = idle;
	(void)zbft_settings_save(ZBFT_KEY_REQ, &idle, sizeof(idle));
	zbft_allow_session_reset();

	/* Pending only after role switch reboot; normal boot stays idle (no RF). */
	if ((req == (uint8_t)ZBFT_MODE_TOOL) || (req == (uint8_t)ZBFT_MODE_UUT)) {
		zbft_start_stack((zbft_mode_t)req);
	} else {
		printk("zbft: mode=idle (zb tool|uut)\n");
		s_mode = ZBFT_MODE_IDLE;
		s_stack_started = false;
		s_net_up = false;
	}

	return STATUS_OK;
}

zbft_mode_t zbft_mode_get(void)
{
	return s_mode;
}

uint8_t zbft_channel_get(void)
{
	return s_channel;
}

bool zbft_stack_started(void)
{
	return s_stack_started;
}

bool zbft_net_up(void)
{
	if (!s_stack_started) {
		return false;
	}
	return s_net_up && ZB_JOINED();
}

zbft_enter_result_t zbft_role_enter(zbft_mode_t mode)
{
	uint8_t v;

	if ((mode != ZBFT_MODE_TOOL) && (mode != ZBFT_MODE_UUT)) {
		return ZBFT_ENTER_FAIL;
	}

	if (!s_stack_started) {
		zbft_start_stack(mode);
		return ZBFT_ENTER_OK;
	}

	if (s_mode == mode) {
		return ZBFT_ENTER_OK;
	}

	/* Switch TOOL <-> UUT: wipe + reboot with one-shot req. */
	v = (uint8_t)mode;
	zbft_settings_load();
	if (!zbft_settings_save(ZBFT_KEY_REQ, &v, sizeof(v))) {
		return ZBFT_ENTER_FAIL;
	}
	(void)zbft_wipe_zboss_nv();
	printk("zbft: switch -> %u reboot\n", (unsigned)mode);
	k_msleep(30);
	sys_reboot(SYS_REBOOT_COLD);
	return ZBFT_ENTER_REBOOTING;
}

bool zbft_channel_set(uint8_t ch)
{
	if ((ch < 11U) || (ch > 26U)) {
		return false;
	}
	if (s_stack_started) {
		return false;
	}
	zbft_settings_load();
	if (!zbft_settings_save(ZBFT_KEY_CH, &ch, sizeof(ch))) {
		return false;
	}
	s_channel = ch;
	return true;
}

int8_t zbft_tx_power_get(void)
{
	return s_tx_power;
}

bool zbft_tx_power_set(int8_t dbm)
{
	if ((dbm < ZBFT_TX_PWR_MIN) || (dbm > ZBFT_TX_PWR_MAX)) {
		return false;
	}
	s_tx_power = dbm;
	if (!s_stack_started) {
		return true;
	}
	return zbft_tx_power_apply();
}

bool zbft_rssi_get(int8_t *rssi_dbm, uint8_t *lqi)
{
	zb_uint16_t peer;
	zb_uint8_t lqa = ZB_MAC_LQI_UNDEFINED;
	zb_int8_t rssi = (zb_int8_t)ZB_MAC_RSSI_UNDEFINED;

	if ((rssi_dbm == NULL) || (lqi == NULL)) {
		return false;
	}
	if (!zbft_net_up()) {
		return false;
	}

	if (s_mode == ZBFT_MODE_TOOL) {
		if (!s_dut_joined) {
			return false;
		}
		peer = s_joined_short;
	} else if (s_mode == ZBFT_MODE_UUT) {
		peer = zb_nwk_get_parent();
		if (peer == 0xFFFFU) {
			return false;
		}
	} else {
		return false;
	}

	zb_zdo_get_diag_data(peer, &lqa, &rssi);
	if ((uint8_t)rssi == ZB_MAC_RSSI_UNDEFINED) {
		return false;
	}

	*rssi_dbm = (int8_t)rssi;
	*lqi = lqa;
	return true;
}

bool zbft_allow_ieee_set(const char *hex16)
{
	zb_ieee_addr_t ieee;
	char ieee_s[24];

	if ((hex16 == NULL) || (s_mode != ZBFT_MODE_TOOL)) {
		return false;
	}
	if (!parse_long_address(hex16, ieee)) {
		return false;
	}

	if (s_allow_ieee_set && s_dut_joined && !ZB_IEEE_ADDR_CMP(ieee, s_allow_ieee)) {
		ZB_IEEE_ADDR_ZERO(s_joined_ieee);
		s_joined_short = 0;
		s_dut_joined = false;
	}

	ZB_IEEE_ADDR_COPY(s_allow_ieee, ieee);
	s_allow_ieee_set = true;
	zbft_ieee_str(ieee, ieee_s, sizeof(ieee_s));
	printk("zbft: allow ieee=%s\n", ieee_s);

	/* Formed already → open join window immediately (one-step allow). */
	if (zbft_net_up()) {
		(void)zbft_join_open(ZBFT_JOIN_SEC_DEFAULT);
	}
	return true;
}

bool zbft_allow_sn_set(const char *sn)
{
	size_t n;

	if ((sn == NULL) || (s_mode != ZBFT_MODE_TOOL)) {
		return false;
	}
	n = strlen(sn);
	if ((n == 0U) || (n >= NVS_SN_SIZE)) {
		return false;
	}
	(void)memcpy(s_allow_sn, sn, n + 1U);
	s_allow_sn_set = true;
	printk("zbft: allow sn=%s\n", s_allow_sn);
	return true;
}

void zbft_allow_clear(void)
{
	zbft_allow_session_reset();
	printk("zbft: allow clear\n");
}

bool zbft_allow_ieee_is_set(void)
{
	return s_allow_ieee_set;
}

bool zbft_allow_sn_is_set(void)
{
	return s_allow_sn_set;
}

const char *zbft_allow_sn_get(void)
{
	return s_allow_sn_set ? s_allow_sn : "";
}

bool zbft_join_open(uint8_t sec)
{
	zb_ret_t ret;

	if (s_mode != ZBFT_MODE_TOOL) {
		return false;
	}
	if (!zbft_net_up()) {
		return false;
	}
	if (!s_allow_ieee_set) {
		return false;
	}
	if ((sec < 1U) || (sec > ZBFT_JOIN_SEC_MAX)) {
		return false;
	}

	ret = zb_buf_get_out_delayed_ext(zbft_permit_join_cb, sec, 0);
	return ret == RET_OK;
}

bool zbft_dut_joined(void)
{
	return s_dut_joined;
}

bool zbft_zbcmd_proxy(const char *line, char *out, size_t out_sz, uint32_t timeout_ms)
{
	size_t n;
	zb_ret_t ret;
	bool ok = false;

	if ((line == NULL) || (s_mode != ZBFT_MODE_TOOL) || !s_dut_joined) {
		return false;
	}
	n = strlen(line);
	if ((n == 0U) || (n >= ZBFT_ZBCMD_MAX)) {
		return false;
	}
	if (k_mutex_lock(&s_zbcmd_lock, K_MSEC(50)) != 0) {
		return false;
	}

	(void)memcpy(s_zbcmd_tx, line, n + 1U);
	s_zbcmd_seq++;
	s_zbcmd_expect_seq = s_zbcmd_seq;
	s_zbcmd_peer_short = s_joined_short;
	s_zbcmd_reply[0] = '\0';
	s_zbcmd_waiting = true;
	(void)k_sem_reset(&s_zbcmd_sem);

	ret = zb_buf_get_out_delayed_ext(zbft_zbcmd_send_frame, ZBFT_CMD_REQ, 0);
	if (ret != RET_OK) {
		s_zbcmd_waiting = false;
		k_mutex_unlock(&s_zbcmd_lock);
		return false;
	}

	if (k_sem_take(&s_zbcmd_sem, K_MSEC(timeout_ms)) == 0) {
		ok = true;
		if ((out != NULL) && (out_sz > 0U)) {
			(void)strncpy(out, s_zbcmd_reply, out_sz - 1U);
			out[out_sz - 1U] = '\0';
		}
	} else {
		s_zbcmd_waiting = false;
	}

	k_mutex_unlock(&s_zbcmd_lock);
	return ok;
}

int zbft_ieee_snprintk(char *buf, size_t buf_len)
{
	zb_ieee_addr_t ieee;

	if ((buf == NULL) || (buf_len == 0U)) {
		return -1;
	}
	if (!s_stack_started) {
		return snprintk(buf, buf_len, "-");
	}
	zb_get_long_address(ieee);
	zbft_ieee_str(ieee, buf, buf_len);
	return (int)strlen(buf);
}

int zbft_status_snprintk(char *buf, size_t buf_len)
{
	const char *mode_s = "idle";
	zb_ieee_addr_t ieee;
	char ieee_s[24];
	char allow_s[24];
	char joined_s[24];
	int n;

	if ((buf == NULL) || (buf_len == 0U)) {
		return -1;
	}

	if (s_mode == ZBFT_MODE_TOOL) {
		mode_s = "tool";
	} else if (s_mode == ZBFT_MODE_UUT) {
		mode_s = "uut";
	}

	ieee_s[0] = '\0';
	if (s_stack_started) {
		zb_get_long_address(ieee);
		zbft_ieee_str(ieee, ieee_s, sizeof(ieee_s));
	} else {
		(void)snprintk(ieee_s, sizeof(ieee_s), "-");
	}

	if (s_allow_ieee_set) {
		zbft_ieee_str(s_allow_ieee, allow_s, sizeof(allow_s));
	} else {
		(void)snprintk(allow_s, sizeof(allow_s), "-");
	}

	if (s_dut_joined) {
		zbft_ieee_str(s_joined_ieee, joined_s, sizeof(joined_s));
	} else {
		(void)snprintk(joined_s, sizeof(joined_s), "-");
	}

	n = snprintk(buf, buf_len,
		     "mode=%s ch=%u pwr=%d started=%u net=%u pan=0x%04x nwk=0x%04x ieee=%s "
		     "allow=%s joined=%s sn=%s",
		     mode_s, (unsigned)s_channel, (int)s_tx_power, s_stack_started ? 1U : 0U,
		     zbft_net_up() ? 1U : 0U,
		     s_stack_started ? (unsigned)ZB_PIBCACHE_PAN_ID() : 0U,
		     s_stack_started ? (unsigned)zb_get_short_address() : 0U, ieee_s, allow_s,
		     joined_s, s_allow_sn_set ? s_allow_sn : "-");
	return n;
}
