/*
 * eink_tag — BLE + EPD project logic (platform boot is in start/).
 *
 * BLE CMD：
 *   PING(1001)         → ACK，不刷屏
 *   DISPLAY(2001)      → 投递 EPD 线程 demo 全刷（异步）
 *   DISP_BEGIN(2002)   → 开会话，清平面，记 len/CRC
 *   DISP_CHUNK(2003)   → 按 offset 写平面
 *   DISP_REFRESH(2004) → CRC 校验后投递 host 全刷
 *   HEARTBEAT(8001)    上行约 20s
 *
 * EPD 在独立线程；上电 ready 后自动跑一次 4.2" demo 全刷。
 * main 只跑 start_poll / BLE。
 * 睡眠：停广播；按键(WAKE)唤醒 / 断连后再开快广播窗口；窗口超时才进睡。
 */

#include "start.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>

#include "app_event.h"
#include "ble.h"
#include "ble_cmd.h"
#include "ble_evt.h"
#include "button.h"
#include "epd_board.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"
#include "pn5180_board.h"

#include <zephyr/sys/crc.h>

#define APP_HB_FLAG_ADV  (1U << 0)
#define APP_HB_FLAG_CONN (1U << 1)

#define APP_LOOP_MS         20U
#define APP_HB_PERIOD_MS    20000U
#define APP_HB_PERIOD_TICKS (APP_HB_PERIOD_MS / APP_LOOP_MS)
/** Discoverable adv window after boot/wake; then sleep if no connect */
#define APP_ADV_WINDOW_MS   30000U

#define APP_EPD_STACK_SIZE  4096
#define APP_EPD_THREAD_PRIO 8
#define APP_EPD_JOB_DEMO    1U
#define APP_EPD_JOB_HOST    2U
#define APP_EPD_ENABLE      1
/** 上电 ready 后自动 demo 全刷（刷在 EPD 线程，不堵 main） */
#define APP_EPD_BOOT_DEMO   1
/** 上电强制 4.2"；仅当 NVS 不是 4.2 时写一次 Flash */
#define APP_EPD_FORCE_4IN2  1

typedef enum {
	APP_DISP_IDLE = 0,
	APP_DISP_RECEIVING,
} app_disp_state_t;

typedef struct {
	app_disp_state_t state;
	uint8_t planes;
	uint16_t bw_len;
	uint16_t red_len;
	uint32_t bw_crc;
	uint32_t red_crc;
} app_disp_session_t;

static K_THREAD_STACK_DEFINE(s_epd_stack, APP_EPD_STACK_SIZE);
static struct k_thread s_epd_thread;
K_MSGQ_DEFINE(s_epd_jobs, sizeof(uint8_t), 2, 1);
static atomic_t s_epd_ready;
static atomic_t s_epd_led_applied;
static app_disp_session_t s_disp;

static int64_t s_adv_deadline_ms;
static bool s_sleeping;

static void app_epd_led_show(uint8_t panel);

/** 鎵撳紑/鍒锋柊鍙彂鐜扮獥鍙ｏ紙蹇箍鎾椂娈碉級銆?*/
static void app_adv_window_arm(void)
{
	s_adv_deadline_ms = k_uptime_get() + (int64_t)APP_ADV_WINDOW_MS;
}

static void app_led_all_off(void)
{
	led_scene_cancel(LED_SCENE_ID_BOOTUP);
	led_scene_cancel(LED_SCENE_ID_EPD_2IN9);
	led_scene_cancel(LED_SCENE_ID_EPD_4IN2);
	led_scene_cancel(LED_SCENE_ID_ERROR);
	led_scene_cancel(LED_SCENE_ID_SUCCESS);
	led_scene_cancel(LED_SCENE_ID_TRIGGER);
	led_scene_cancel(LED_SCENE_ID_PAIRING);
	led_scene_led_direct_set(LED_SCENE_LED_0, false);
	led_scene_led_direct_set(LED_SCENE_LED_1, false);
}

static void app_sleep_enter(void)
{
	status_t st;

	k_event_clear(&app_events, APP_EVT_ANY);
	app_led_all_off();

	st = ble_adv_stop();
	if (st != STATUS_OK) {
		LOG_WARN("app: adv stop failed st=%d", (int)st);
	}

	st = epd_board_sleep();
	if (st != STATUS_OK) {
		LOG_WARN("app: epd sleep skipped st=%d", (int)st);
	}

	button_wake_enable();
	s_sleeping = true;
	atomic_set(&s_epd_led_applied, 0);

	if (pn5180_board_scan_is_active()) {
		(void)pn5180_board_scan_stop();
	}

	LOG_INFO("app: SLEEP enter (adv off, epd sleep, wake=BTN_PERMISSION_WAKE)");
}

static void app_sleep_exit(void)
{
	status_t st;

	s_sleeping = false;
	button_wake_disable();

	(void)ble_adv_set_slow(false);
	st = ble_adv_start();
	if (st != STATUS_OK) {
		LOG_WARN("app: adv start after wake failed st=%d", (int)st);
	}
	app_adv_window_arm();

	if (atomic_get(&s_epd_ready) != 0) {
		app_epd_led_show(epd_board_size());
		atomic_set(&s_epd_led_applied, 1);
	}

	LOG_INFO("app: SLEEP exit 鈫?advertising %us window",
		 (unsigned int)(APP_ADV_WINDOW_MS / 1000U));
}

static bool app_sleep_can_enter(void)
{
	if (s_sleeping) {
		return false;
	}
#if APP_EPD_ENABLE
	if (atomic_get(&s_epd_ready) == 0) {
		return false;
	}
	if (epd_board_is_busy()) {
		return false;
	}
	if (s_disp.state != APP_DISP_IDLE) {
		return false;
	}
	if (k_msgq_num_used_get(&s_epd_jobs) != 0U) {
		return false;
	}
#endif
	if (ble_is_connected()) {
		return false;
	}
	if (pn5180_board_scan_is_active()) {
		return false;
	}
	/* 骞挎挱绐楀彛鏈埌锛氫繚鎸佸彲鍙戠幇 */
	if (k_uptime_get() < s_adv_deadline_ms) {
		return false;
	}
	return true;
}

static void app_ble_evt_cb(const ble_evt_t *evt, void *user)
{
	ARG_UNUSED(user);

	if (evt == NULL) {
		return;
	}

	if (evt->id == BLE_EVT_CONNECTED) {
		k_event_post(&app_events, APP_EVT_BLE_GAP);
	} else if (evt->id == BLE_EVT_DISCONNECTED) {
		/* Restart adv after disconnect; ADV_WINDOW then sleep */
		app_adv_window_arm();
		k_event_post(&app_events, APP_EVT_BLE_GAP);
	}
}

static void app_epd_led_show(uint8_t panel)
{
	led_scene_cancel(LED_SCENE_ID_BOOTUP);
	led_scene_cancel(LED_SCENE_ID_EPD_2IN9);
	led_scene_cancel(LED_SCENE_ID_EPD_4IN2);

	if (panel == NVS_EPD_PANEL_4IN2) {
		led_scene_run(LED_SCENE_ID_EPD_4IN2);
		LOG_INFO("app: LED 鈫?4.2in (blue solid)");
	} else {
		led_scene_run(LED_SCENE_ID_EPD_2IN9);
		LOG_INFO("app: LED 鈫?2.9in (green solid)");
	}
}

static void app_epd_panel_switch(uint8_t panel)
{
	uint8_t cur = nvs_epd_panel_get();

	if ((panel != NVS_EPD_PANEL_2IN9) && (panel != NVS_EPD_PANEL_4IN2)) {
		return;
	}

	app_epd_led_show(panel);

	if (cur == panel) {
		LOG_INFO("app: epd panel already %s",
			 (panel == NVS_EPD_PANEL_4IN2) ? "4.2in" : "2.9in");
		return;
	}

	if (!nvs_epd_panel_set(panel)) {
		LOG_ERROR("app: nvs_epd_panel_set(%u) failed", (unsigned)panel);
		led_scene_run(LED_SCENE_ID_ERROR);
		return;
	}

	LOG_INFO("app: epd panel 鈫?%s saved, reboot",
		 (panel == NVS_EPD_PANEL_4IN2) ? "4.2in" : "2.9in");
	k_msleep(500);
	sys_reboot(SYS_REBOOT_COLD);
}

static void app_handle_button(void)
{
	btn_id_e id;
	btn_event_e ev;
	status_t st;

	if (!app_event_take(APP_EVT_BUTTON)) {
		return;
	}

	app_adv_window_arm();
	button_last_event_get(&id, &ev);
	button_last_event_clear();

	/* BTN2=BACK 鍗曞嚮锛歅N5180 鎵崱寮€/鍏?*/
	if ((id == BTN_ID_BACK) && (ev == BTN_EVENT_SINGLE_CLICK)) {
		if (!pn5180_board_is_ready()) {
			LOG_WARN("app: BTN2 scan ignored (pn5180 not ready)");
			return;
		}
		if (pn5180_board_scan_is_active()) {
			st = pn5180_board_scan_stop();
			LOG_INFO("app: BTN2 鈫?scan stop st=%d", (int)st);
		} else {
			st = pn5180_board_scan_start();
			LOG_INFO("app: BTN2 鈫?scan start st=%d", (int)st);
		}
		return;
	}

	/* BTN1=CONFIRM锛氬弻鍑烩啋4.2"锛岄暱鎸?0s鈫?.9"锛涘甫 WAKE 鏉冮檺鐨勯敭鍙粠鐫＄湢鍞ら啋骞跺紑骞挎挱 */
	if (id != BTN_ID_CONFIRM) {
		return;
	}

	if (ev == BTN_EVENT_DOUBLE_CLICK) {
		LOG_INFO("app: BTN double 鈫?4.2in");
		app_epd_panel_switch(NVS_EPD_PANEL_4IN2);
	} else if (ev == BTN_EVENT_LONG_PRESS) {
		LOG_INFO("app: BTN long 鈫?2.9in");
		app_epd_panel_switch(NVS_EPD_PANEL_2IN9);
	}
}

static status_t app_epd_queue_job(uint8_t job)
{
	status_t st;

	if (atomic_get(&s_epd_ready) == 0) {
		LOG_WARN("app: EPD not ready yet");
		return STATUS_INVALID_STATE;
	}

	st = epd_board_lock_planes();
	if (st != STATUS_OK) {
		LOG_WARN("app: EPD planes busy");
		return st;
	}

	if (k_msgq_put(&s_epd_jobs, &job, K_NO_WAIT) != 0) {
		epd_board_unlock_planes();
		LOG_WARN("app: EPD job queue full");
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

static void app_disp_reset(void)
{
	memset(&s_disp, 0, sizeof(s_disp));
	s_disp.state = APP_DISP_IDLE;
}

static status_t app_disp_handle_begin(const uint8_t *payload, uint16_t plen)
{
	uint16_t width;
	uint16_t height;
	uint8_t planes;
	uint8_t flags;
	uint16_t bw_len;
	uint16_t red_len;
	uint32_t bw_crc;
	uint32_t red_crc;
	uint32_t plane_sz;
	status_t st;

	if (plen != BLE_CMD_DISP_BEGIN_PAYLOAD_SIZE) {
		LOG_WARN("app: BEGIN bad plen=%u (need %u)", (unsigned)plen,
			 (unsigned)BLE_CMD_DISP_BEGIN_PAYLOAD_SIZE);
		return STATUS_INVALID_ARG;
	}
	if (atomic_get(&s_epd_ready) == 0) {
		LOG_WARN("app: BEGIN rejected 鈥?EPD not ready yet (wait for 'epd: ready')");
		return STATUS_INVALID_STATE;
	}
	if (epd_board_is_busy()) {
		LOG_WARN("app: BEGIN rejected 鈥?EPD busy (refresh in progress)");
		return STATUS_INVALID_STATE;
	}

	width = sys_get_le16(&payload[0]);
	height = sys_get_le16(&payload[2]);
	planes = payload[4];
	flags = payload[5];
	bw_len = sys_get_le16(&payload[6]);
	red_len = sys_get_le16(&payload[8]);
	bw_crc = sys_get_le32(&payload[10]);
	red_crc = sys_get_le32(&payload[14]);

	plane_sz = epd_board_plane_size();

	if ((width != epd_board_width()) || (height != epd_board_height())) {
		LOG_WARN("app: BEGIN size mismatch %ux%u (board %ux%u)", width, height,
			 epd_board_width(), epd_board_height());
		return STATUS_INVALID_ARG;
	}
	if (flags != 0U) {
		return STATUS_NOT_SUPPORTED;
	}
	if ((planes & (BLE_CMD_DISP_PLANES_BW | BLE_CMD_DISP_PLANES_RED)) == 0U) {
		return STATUS_INVALID_ARG;
	}
	if ((planes & ~((uint8_t)(BLE_CMD_DISP_PLANES_BW | BLE_CMD_DISP_PLANES_RED))) != 0U) {
		return STATUS_INVALID_ARG;
	}

	if ((planes & BLE_CMD_DISP_PLANES_BW) != 0U) {
		if (bw_len != (uint16_t)plane_sz) {
			return STATUS_INVALID_ARG;
		}
	} else if ((bw_len != 0U) || (bw_crc != 0U)) {
		return STATUS_INVALID_ARG;
	}

	if ((planes & BLE_CMD_DISP_PLANES_RED) != 0U) {
		if (red_len != (uint16_t)plane_sz) {
			return STATUS_INVALID_ARG;
		}
	} else if ((red_len != 0U) || (red_crc != 0U)) {
		return STATUS_INVALID_ARG;
	}

	st = epd_board_clear_planes(EPD_BOARD_PLANES_BW | EPD_BOARD_PLANES_RED);
	if (st != STATUS_OK) {
		return st;
	}

	s_disp.state = APP_DISP_RECEIVING;
	s_disp.planes = planes;
	s_disp.bw_len = bw_len;
	s_disp.red_len = red_len;
	s_disp.bw_crc = bw_crc;
	s_disp.red_crc = red_crc;

	LOG_INFO("app: BEGIN ok planes=0x%02x bw_len=%u red_len=%u", planes, bw_len, red_len);
	return STATUS_OK;
}

static status_t app_disp_handle_chunk(const uint8_t *payload, uint16_t plen)
{
	uint8_t plane;
	uint16_t offset;
	uint16_t data_len;
	uint8_t expect_bit;

	if (s_disp.state != APP_DISP_RECEIVING) {
		return STATUS_INVALID_STATE;
	}
	if (epd_board_is_busy()) {
		return STATUS_INVALID_STATE;
	}
	if (plen < BLE_CMD_DISP_CHUNK_HDR_SIZE) {
		return STATUS_INVALID_ARG;
	}

	plane = payload[0];
	offset = sys_get_le16(&payload[1]);
	data_len = sys_get_le16(&payload[3]);

	if ((uint16_t)(BLE_CMD_DISP_CHUNK_HDR_SIZE + data_len) != plen) {
		return STATUS_INVALID_ARG;
	}
	if (data_len == 0U) {
		return STATUS_INVALID_ARG;
	}
	if (data_len > BLE_CMD_DISP_CHUNK_DATA_MAX) {
		return STATUS_INVALID_ARG;
	}

	if (plane == BLE_CMD_DISP_PLANE_BW) {
		expect_bit = BLE_CMD_DISP_PLANES_BW;
	} else if (plane == BLE_CMD_DISP_PLANE_RED) {
		expect_bit = BLE_CMD_DISP_PLANES_RED;
	} else {
		return STATUS_INVALID_ARG;
	}

	if ((s_disp.planes & expect_bit) == 0U) {
		return STATUS_INVALID_ARG;
	}

	return epd_board_write_plane(plane, offset, &payload[BLE_CMD_DISP_CHUNK_HDR_SIZE], data_len);
}

static status_t app_disp_verify_crc(void)
{
	const uint8_t *data;
	uint32_t got;

	if ((s_disp.planes & BLE_CMD_DISP_PLANES_BW) != 0U) {
		data = epd_board_plane_data(EPD_BOARD_PLANE_BW);
		if (data == NULL) {
			return STATUS_INVALID_STATE;
		}
		got = crc32_ieee(data, s_disp.bw_len);
		if (got != s_disp.bw_crc) {
			LOG_WARN("app: BW CRC fail got=0x%08x expect=0x%08x", got, s_disp.bw_crc);
			return STATUS_FAIL;
		}
	}

	if ((s_disp.planes & BLE_CMD_DISP_PLANES_RED) != 0U) {
		data = epd_board_plane_data(EPD_BOARD_PLANE_RED);
		if (data == NULL) {
			return STATUS_INVALID_STATE;
		}
		got = crc32_ieee(data, s_disp.red_len);
		if (got != s_disp.red_crc) {
			LOG_WARN("app: RED CRC fail got=0x%08x expect=0x%08x", got, s_disp.red_crc);
			return STATUS_FAIL;
		}
	}

	return STATUS_OK;
}

static status_t app_disp_handle_refresh(uint16_t plen)
{
	status_t st;

	if (plen != 0U) {
		return STATUS_INVALID_ARG;
	}
	if (s_disp.state != APP_DISP_RECEIVING) {
		return STATUS_INVALID_STATE;
	}
	if (epd_board_is_busy()) {
		return STATUS_INVALID_STATE;
	}

	st = app_disp_verify_crc();
	if (st != STATUS_OK) {
		app_disp_reset();
		return st;
	}

	/* 鍏ラ槦鍓嶅凡 lock锛屽埛瀹岀敱 worker 鍐?refresh 瑙ｉ攣 */
	st = app_epd_queue_job(APP_EPD_JOB_HOST);
	app_disp_reset();
	if (st == STATUS_OK) {
		LOG_INFO("app: REFRESH queued (host buffer)");
	}
	return st;
}

static void app_ack(uint16_t cmd_id, status_t st)
{
	status_t send_st = ble_cmd_send_ack(cmd_id, st);

	if (send_st == STATUS_INVALID_STATE) {
		LOG_INFO("app: ACK skipped (enable TX Notify)");
	} else if (send_st != STATUS_OK) {
		LOG_WARN("app: ACK send failed %d", (int)send_st);
	}
}

static void app_handle_ble_cmds(void)
{
	ble_cmd_msg_t msg;
	status_t st;

	while (ble_cmd_fetch(&msg)) {
		/* 涓氬姟娲诲姩涓嶅欢闀垮彲鍙戠幇绐楀彛锛涗粎杩炴帴涓湁鏁?*/
		LOG_INFO("app: CMD recv id=%u plen=%u", (unsigned int)msg.cmd_id,
			 (unsigned int)msg.payload_len);

		switch (msg.cmd_id) {
		case BLE_CMD_PING:
			LOG_INFO("app: PING ok (link only, no EPD)");
			app_ack(BLE_CMD_PING, STATUS_OK);
			break;

		case BLE_CMD_DISPLAY:
			LOG_INFO("app: DISPLAY 鈫?queue demo refresh");
			st = app_epd_queue_job(APP_EPD_JOB_DEMO);
			app_ack(BLE_CMD_DISPLAY, st);
			break;

		case BLE_CMD_DISP_BEGIN:
			st = app_disp_handle_begin(msg.payload, msg.payload_len);
			app_ack(BLE_CMD_DISP_BEGIN, st);
			break;

		case BLE_CMD_DISP_CHUNK:
			st = app_disp_handle_chunk(msg.payload, msg.payload_len);
			app_ack(BLE_CMD_DISP_CHUNK, st);
			break;

		case BLE_CMD_DISP_REFRESH:
			st = app_disp_handle_refresh(msg.payload_len);
			app_ack(BLE_CMD_DISP_REFRESH, st);
			break;

		default:
			LOG_WARN("app: unknown cmd_id=%u", (unsigned int)msg.cmd_id);
			app_ack(msg.cmd_id, STATUS_NOT_SUPPORTED);
			break;
		}
	}
}

static void app_ble_cmd_smoke_inject(void)
{
	uint8_t frame[BLE_CMD_HDR_SIZE];

	sys_put_le16(BLE_CMD_PING, &frame[0]);
	sys_put_le16(0U, &frame[2]);

	if (ble_cmd_ingress(frame, sizeof(frame)) != STATUS_OK) {
		LOG_WARN("app: ping inject failed");
		return;
	}

	LOG_INFO("app: injected BLE_CMD_PING (1001)");
}

static void app_ble_heartbeat_try(uint32_t hb_tick)
{
	uint8_t flags = 0U;
	status_t st;

	if (!ble_is_connected()) {
		return;
	}

	if (ble_is_advertising()) {
		flags |= APP_HB_FLAG_ADV;
	}
	flags |= APP_HB_FLAG_CONN;

	st = ble_cmd_send_heartbeat(hb_tick, flags);
	if (st == STATUS_OK) {
		LOG_INFO("app: HEARTBEAT up tick=%u flags=0x%02x", hb_tick, flags);
	} else if (st != STATUS_INVALID_STATE) {
		LOG_WARN("app: HEARTBEAT send failed %d", (int)st);
	}
}

static void app_epd_worker(void *p1, void *p2, void *p3)
{
	status_t st;
	uint8_t job;
	uint32_t refresh_n = 0U;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INFO("epd: worker start");

	st = epd_board_spi_selftest();
	if (st != STATUS_OK) {
		LOG_ERROR("epd_board_spi_selftest failed: %d", (int)st);
		return;
	}

	st = epd_board_init();
	if (st != STATUS_OK) {
		LOG_ERROR("epd_board_init failed: %d", (int)st);
		return;
	}

	atomic_set(&s_epd_ready, 1);
	LOG_INFO("epd: ready board=%ux%u plane=%u panel=%s — DISPLAY=demo | BEGIN/CHUNK/REFRESH",
		 (unsigned)epd_board_width(), (unsigned)epd_board_height(),
		 (unsigned)epd_board_plane_size(), epd_board_panel_name());

#if APP_EPD_BOOT_DEMO
	/* 上电 demo：在本线程直接刷，不经队列（避免与后续 DISPLAY 抢锁时序） */
	LOG_INFO("epd: boot demo start");
	st = epd_board_test_run();
	if (st != STATUS_OK) {
		LOG_ERROR("epd: boot demo failed: %d", (int)st);
	} else {
		LOG_INFO("epd: boot demo done");
	}
#endif

	for (;;) {
		if (k_msgq_get(&s_epd_jobs, &job, K_FOREVER) != 0) {
			continue;
		}

		refresh_n++;
		if (job == APP_EPD_JOB_DEMO) {
			LOG_INFO("epd: job #%u DEMO start", (unsigned int)refresh_n);
			st = epd_board_test_run();
		} else if (job == APP_EPD_JOB_HOST) {
			LOG_INFO("epd: job #%u HOST refresh start", (unsigned int)refresh_n);
			st = epd_board_refresh();
		} else {
			LOG_WARN("epd: unknown job %u", (unsigned int)job);
			continue;
		}

		if (st != STATUS_OK) {
			LOG_ERROR("epd: job #%u failed: %d", (unsigned int)refresh_n, (int)st);
		} else {
			LOG_INFO("epd: job #%u done", (unsigned int)refresh_n);
		}
	}
}

static status_t app_epd_start_async(void)
{
	k_tid_t tid;

	atomic_set(&s_epd_ready, 0);
	atomic_set(&s_epd_led_applied, 0);
	app_disp_reset();

	tid = k_thread_create(&s_epd_thread, s_epd_stack, K_THREAD_STACK_SIZEOF(s_epd_stack),
			      app_epd_worker, NULL, NULL, NULL, K_PRIO_PREEMPT(APP_EPD_THREAD_PRIO),
			      0, K_NO_WAIT);
	if (tid == NULL) {
		LOG_ERROR("epd: thread create failed");
		return STATUS_FAIL;
	}

	k_thread_name_set(tid, "epd");
	LOG_INFO("epd: async worker created");
	return STATUS_OK;
}

status_t app_init(void)
{
	status_t st;
	uint8_t panel;

	LOG_INFO("pins UART P1.04/P1.05 | LED_G P2.08 LED_B P2.09 | BTN P0.00/P0.01");
	LOG_INFO("epd SPI00 SCK=P2.01 MOSI=P2.02 CS=P2.05 | DC=P2.00 RST=P2.03 BUSY=P2.07");
	LOG_INFO("pn5180 SPI22 SCK=P1.11 MOSI=P1.12 MISO=P1.13 NSS=P1.14 | BUSY=P1.08 RST=P1.07");

	/* Keep BOOTUP G/B blink until EPD ready, then solid size LED */
	panel = nvs_epd_panel_get();
	LOG_INFO("app: NVS epanel=%u (%s) -- dbl=4.2in long10s=2.9in", (unsigned)panel,
		 (panel == NVS_EPD_PANEL_4IN2) ? "4.2in" : "2.9in");

#if APP_EPD_ENABLE && APP_EPD_FORCE_4IN2
	if (panel != NVS_EPD_PANEL_4IN2) {
		if (!nvs_epd_panel_set(NVS_EPD_PANEL_4IN2)) {
			LOG_ERROR("app: force 4.2in NVS write failed");
		} else {
			LOG_INFO("app: force epanel -> 4.2in (NVS updated)");
			panel = NVS_EPD_PANEL_4IN2;
		}
	}
#endif

	ble_evt_set_callback(app_ble_evt_cb, NULL);
	/* 断连后自动再广播，由 ADV_WINDOW 超时后再停广播进睡 */
	ble_adv_set_restart_on_disconnect(true);
	app_adv_window_arm();

	st = ble_init();
	if (st != STATUS_OK) {
		LOG_ERROR("ble_init failed: %d", (int)st);
		return STATUS_FAIL;
	}

#if APP_EPD_ENABLE
	st = app_epd_start_async();
	if (st != STATUS_OK) {
		(void)ble_stop();
		return STATUS_FAIL;
	}
#else
	LOG_INFO("app: EPD init skipped (APP_EPD_ENABLE=0)");
#endif

	/* PN5180: SPI smoke + scan thread; BTN2 click toggles scan */
	st = pn5180_board_smoke();
	if (st != STATUS_OK) {
		LOG_ERROR("pn5180_board_smoke failed: %d (SPI/鎺ョ嚎/渚涚數?)", (int)st);
	} else {
		st = pn5180_board_scan_init();
		if (st != STATUS_OK) {
			LOG_ERROR("pn5180_board_scan_init failed: %d", (int)st);
		} else {
			LOG_INFO("app: BTN2(P0.01) single-click = PN5180 scan toggle");
		}
	}

	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("eink_tag run: PING | DISPLAY=demo | BEGIN/CHUNK/REFRESH | HB=%us",
		 (unsigned int)(APP_HB_PERIOD_MS / 1000U));
	LOG_INFO("eink_tag btn: CONFIRM dbl->4.2in | long10s->2.9in | BACK click->NFC scan");
	LOG_INFO("eink_tag sleep: adv window %us; disc->re-adv %us then sleep; wake=WAKE btn",
		 (unsigned int)(APP_ADV_WINDOW_MS / 1000U),
		 (unsigned int)(APP_ADV_WINDOW_MS / 1000U));

	app_ble_cmd_smoke_inject();
	app_adv_window_arm();

	while (1) {
		start_poll();

		/* After EPD ready: cancel boot blink, show size LED */
		if ((atomic_get(&s_epd_ready) != 0) && (atomic_cas(&s_epd_led_applied, 0, 1))) {
			app_epd_led_show(epd_board_size());
		}

		app_handle_button();

		{
			uint32_t ev = k_event_wait(&app_events, APP_EVT_BLE_GAP | APP_EVT_BLE_CMD,
						   false, K_NO_WAIT);

			if ((ev & APP_EVT_BLE_GAP) != 0U) {
				k_event_clear(&app_events, APP_EVT_BLE_GAP);
				/* connect/disconnect already adjusted window in callback */
			}
			if ((ev & APP_EVT_BLE_CMD) != 0U) {
				k_event_clear(&app_events, APP_EVT_BLE_CMD);
				app_handle_ble_cmds();
			}
		}

		if ((tick % APP_HB_PERIOD_TICKS) == 0U) {
			LOG_INFO("status tick=%u adv=%d conn=%d sleep=%d epd_ready=%d "
				 "epd_busy=%d nfc_scan=%d size=%s",
				 tick / APP_HB_PERIOD_TICKS, ble_is_advertising() ? 1 : 0,
				 ble_is_connected() ? 1 : 0, s_sleeping ? 1 : 0,
				 (int)atomic_get(&s_epd_ready), epd_board_is_busy() ? 1 : 0,
				 pn5180_board_scan_is_active() ? 1 : 0,
				 (epd_board_size() == EPD_BOARD_SIZE_4IN2) ? "4.2in" : "2.9in");
			app_ble_heartbeat_try(tick / APP_HB_PERIOD_TICKS);
		}

		if (app_sleep_can_enter()) {
			app_sleep_enter();
			(void)k_event_wait(&app_events, APP_EVT_ANY, false, K_FOREVER);
			app_sleep_exit();
			continue;
		}

		tick++;
		k_sleep(K_MSEC(APP_LOOP_MS));
	}
}

