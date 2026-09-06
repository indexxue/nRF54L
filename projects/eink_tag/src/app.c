/*
 * eink_tag — BLE + EPD project logic (platform boot is in start/).
 *
 * BLE CMD：
 *   PING(1001)    → ACK，不刷屏
 *   DISPLAY(2001) → 投递 EPD 线程全刷 demo（异步）
 *   HEARTBEAT(8001) 上行约 20s
 *
 * EPD 在独立线程；main 只跑 start_poll / BLE。
 */

#include "start.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include "ble.h"
#include "ble_cmd.h"
#include "epd_board.h"
#include "event.h"
#include "log.h"

#define APP_HB_FLAG_ADV  (1U << 0)
#define APP_HB_FLAG_CONN (1U << 1)

#define APP_LOOP_MS         20U
#define APP_HB_PERIOD_MS    20000U
#define APP_HB_PERIOD_TICKS (APP_HB_PERIOD_MS / APP_LOOP_MS)

#define APP_EPD_STACK_SIZE  4096
#define APP_EPD_THREAD_PRIO 8
#define APP_EPD_JOB_REFRESH 1U

static K_THREAD_STACK_DEFINE(s_epd_stack, APP_EPD_STACK_SIZE);
static struct k_thread s_epd_thread;
K_MSGQ_DEFINE(s_epd_jobs, sizeof(uint8_t), 2, 1);
static atomic_t s_epd_ready;

static status_t app_epd_request_refresh(void)
{
	uint8_t job = APP_EPD_JOB_REFRESH;

	if (atomic_get(&s_epd_ready) == 0) {
		LOG_WARN("app: EPD not ready yet");
		return STATUS_INVALID_STATE;
	}

	if (k_msgq_put(&s_epd_jobs, &job, K_NO_WAIT) != 0) {
		LOG_WARN("app: EPD job queue full (refresh busy)");
		return STATUS_FAIL;
	}

	LOG_INFO("app: EPD refresh queued");
	return STATUS_OK;
}

static void app_handle_ble_cmds(void)
{
	ble_cmd_msg_t msg;
	status_t st;

	while (ble_cmd_fetch(&msg)) {
		LOG_INFO("app: CMD recv id=%u plen=%u", (unsigned int)msg.cmd_id,
			 (unsigned int)msg.payload_len);

		switch (msg.cmd_id) {
		case BLE_CMD_PING:
			LOG_INFO("app: PING ok (link only, no EPD)");
			st = ble_cmd_send_ack(BLE_CMD_PING, STATUS_OK);
			if (st == STATUS_INVALID_STATE) {
				LOG_INFO("app: ACK skipped (enable TX Notify)");
			} else if (st != STATUS_OK) {
				LOG_WARN("app: ACK send failed %d", (int)st);
			} else {
				LOG_INFO("app: ACK sent for PING");
			}
			break;

		case BLE_CMD_DISPLAY:
			/* payload 暂忽略：触发一次 demo 全刷（后续再接图像数据） */
			LOG_INFO("app: DISPLAY → queue EPD refresh (payload ignored for now)");
			st = app_epd_request_refresh();
			(void)ble_cmd_send_ack(BLE_CMD_DISPLAY, st);
			if (st == STATUS_OK) {
				LOG_INFO("app: ACK sent for DISPLAY (refresh async)");
			}
			break;

		default:
			LOG_WARN("app: unknown cmd_id=%u", (unsigned int)msg.cmd_id);
			(void)ble_cmd_send_ack(msg.cmd_id, STATUS_NOT_SUPPORTED);
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

	st = epd_board_test_run();
	if (st != STATUS_OK) {
		LOG_ERROR("epd: boot demo failed: %d", (int)st);
		return;
	}

	atomic_set(&s_epd_ready, 1);
	LOG_INFO("epd: ready — waiting for DISPLAY(2001) refresh jobs");

	for (;;) {
		if (k_msgq_get(&s_epd_jobs, &job, K_FOREVER) != 0) {
			continue;
		}

		if (job != APP_EPD_JOB_REFRESH) {
			LOG_WARN("epd: unknown job %u", (unsigned int)job);
			continue;
		}

		refresh_n++;
		LOG_INFO("epd: refresh #%u start (from BLE DISPLAY)", (unsigned int)refresh_n);
		st = epd_board_test_run();
		if (st != STATUS_OK) {
			LOG_ERROR("epd: refresh #%u failed: %d", (unsigned int)refresh_n, (int)st);
		} else {
			LOG_INFO("epd: refresh #%u done", (unsigned int)refresh_n);
		}
	}
}

static status_t app_epd_start_async(void)
{
	k_tid_t tid;

	atomic_set(&s_epd_ready, 0);

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

	LOG_INFO("pins UART P1.09/P1.10 | LED_G P1.05 LED_B P1.04 | BTN P0.00/P0.01");
	LOG_INFO("epd SPI00 SCK=P2.01 MOSI=P2.04 CS=P2.05 | DC=P1.08 RST=P1.07 BUSY=P1.06");

	st = ble_init();
	if (st != STATUS_OK) {
		LOG_ERROR("ble_init failed: %d", (int)st);
		return STATUS_FAIL;
	}

	st = app_epd_start_async();
	if (st != STATUS_OK) {
		(void)ble_stop();
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("eink_tag run: PING=link | DISPLAY=EPD refresh | HB=%us",
		 (unsigned int)(APP_HB_PERIOD_MS / 1000U));

	app_ble_cmd_smoke_inject();

	while (1) {
		start_poll();

		if (event_is_set(EVT_ID_BLE_CMD)) {
			app_handle_ble_cmds();
		}

		if ((tick % APP_HB_PERIOD_TICKS) == 0U) {
			LOG_INFO("status tick=%u adv=%d conn=%d epd_ready=%d",
				 tick / APP_HB_PERIOD_TICKS, ble_is_advertising() ? 1 : 0,
				 ble_is_connected() ? 1 : 0, (int)atomic_get(&s_epd_ready));
			app_ble_heartbeat_try(tick / APP_HB_PERIOD_TICKS);
		}
		tick++;
		k_sleep(K_MSEC(APP_LOOP_MS));
	}
}
