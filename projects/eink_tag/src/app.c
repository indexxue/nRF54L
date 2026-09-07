/*
 * eink_tag — BLE + EPD project logic (platform boot is in start/).
 *
 * BLE CMD：
 *   PING(1001)         → ACK，不刷屏
 *   DISPLAY(2001)      → 投递 EPD 线程 demo 全刷（异步；上电不自动跑）
 *   DISP_BEGIN(2002)   → 开会话，清平面，记 len/CRC
 *   DISP_CHUNK(2003)   → 按 offset 写平面
 *   DISP_REFRESH(2004) → CRC 校验后投递 host 全刷
 *   HEARTBEAT(8001)    上行约 20s
 *
 * EPD 在独立线程；main 只跑 start_poll / BLE。
 */

#include "start.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include "ble.h"
#include "ble_cmd.h"
#include "crc32.h"
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
#define APP_EPD_JOB_DEMO    1U
#define APP_EPD_JOB_HOST    2U

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
static app_disp_session_t s_disp;

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
		LOG_WARN("app: BEGIN rejected — EPD not ready yet (wait for 'epd: ready')");
		return STATUS_INVALID_STATE;
	}
	if (epd_board_is_busy()) {
		LOG_WARN("app: BEGIN rejected — EPD busy (refresh in progress)");
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
		got = crc32_compute(data, s_disp.bw_len);
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
		got = crc32_compute(data, s_disp.red_len);
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

	/* 入队前已 lock，刷完由 worker 内 refresh 解锁 */
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
		LOG_INFO("app: CMD recv id=%u plen=%u", (unsigned int)msg.cmd_id,
			 (unsigned int)msg.payload_len);

		switch (msg.cmd_id) {
		case BLE_CMD_PING:
			LOG_INFO("app: PING ok (link only, no EPD)");
			app_ack(BLE_CMD_PING, STATUS_OK);
			break;

		case BLE_CMD_DISPLAY:
			LOG_INFO("app: DISPLAY → queue demo refresh");
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

	/* 上电不跑 demo；DISPLAY(2001) / Host BEGIN→REFRESH 再刷 */
	atomic_set(&s_epd_ready, 1);
	LOG_INFO("epd: ready board=%ux%u plane=%u — DISPLAY=demo | BEGIN/CHUNK/REFRESH",
		 (unsigned)epd_board_width(), (unsigned)epd_board_height(),
		 (unsigned)epd_board_plane_size());

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

	LOG_INFO("eink_tag run: PING | DISPLAY=demo | BEGIN/CHUNK/REFRESH | HB=%us",
		 (unsigned int)(APP_HB_PERIOD_MS / 1000U));

	app_ble_cmd_smoke_inject();

	while (1) {
		start_poll();

		if (event_is_set(EVT_ID_BLE_CMD)) {
			app_handle_ble_cmds();
		}

		if ((tick % APP_HB_PERIOD_TICKS) == 0U) {
			LOG_INFO("status tick=%u adv=%d conn=%d epd_ready=%d epd_busy=%d",
				 tick / APP_HB_PERIOD_TICKS, ble_is_advertising() ? 1 : 0,
				 ble_is_connected() ? 1 : 0, (int)atomic_get(&s_epd_ready),
				 epd_board_is_busy() ? 1 : 0);
			app_ble_heartbeat_try(tick / APP_HB_PERIOD_TICKS);
		}
		tick++;
		k_sleep(K_MSEC(APP_LOOP_MS));
	}
}
