/*
 * ink_panel — BLE 平面上传 + EPD + W25Q 页槽 + CAP1203
 *
 * BLE：PING / GET_SLOT_STATUS / DISPLAY / BEGIN(20B)·CHUNK·REFRESH /
 *       SET_GROUP_META / HEARTBEAT / SLOT_STATUS
 * 触摸：CS1=L 按 content_count 翻页；CS2=R 切模 home→notes@1→info@1；开机刷 home/0。
 * BTN1 单击断连或再广播，长按 5s → factory（不对屏）。
 */

#include "start.h"

#include <stdbool.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "app_event.h"
#include "ble.h"
#include "ble_cmd.h"
#include "ble_evt.h"
#include "board.h"
#include "button.h"
#include "npm1300_board.h"
#include "nvs.h"

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
#include "epd_board.h"
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
#include "page_slot.h"
#include "w25q_board.h"
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_CAP1203)
#include "cap1203_board.h"
#endif

#define APP_HB_FLAG_ADV  (1U << 0)
#define APP_HB_FLAG_CONN (1U << 1)

#define APP_LOOP_MS      20U
#define APP_HB_PERIOD_MS 20000U

static struct k_work_delayable s_hb_work;
static uint32_t s_hb_seq;

#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
static atomic_t s_slot_status_pending;
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
/** 本机当前显的槽（触摸导航 / Host show=1 同步） */
static uint8_t s_view_mode = PAGE_SLOT_MODE_HOME;
static uint8_t s_view_page;

static void app_view_set(uint8_t mode, uint8_t page)
{
	s_view_mode = mode;
	s_view_page = page;
}

static status_t app_view_show(uint8_t mode, uint8_t page)
{
	status_t st;

	if (!page_slot_is_content_page(mode, page)) {
		printk("app: view reject mode=%u page=%u (not content)\n", (unsigned)mode,
		       (unsigned)page);
		return STATUS_INVALID_ARG;
	}

	st = epd_board_show_slot_request(mode, page);
	if (st == STATUS_OK) {
		app_view_set(mode, page);
		printk("app: view mode=%u page=%u\n", (unsigned)mode, (unsigned)page);
	} else {
		printk("app: view show fail st=%d mode=%u page=%u\n", (int)st,
		       (unsigned)mode, (unsigned)page);
	}
	return st;
}

/** L：仅 notes/info 在 1..content_count 循环 */
static void app_view_page_next(void)
{
	uint8_t count;
	uint8_t next;

	if (s_view_mode == PAGE_SLOT_MODE_HOME) {
		printk("app: touch L ignored (home)\n");
		return;
	}

	count = page_slot_group_content_count(s_view_mode);
	if (count == 0U) {
		printk("app: touch L ignored (mode=%u count=0)\n", (unsigned)s_view_mode);
		return;
	}
	if (count == 1U) {
		printk("app: touch L ignored (mode=%u single content page)\n",
		       (unsigned)s_view_mode);
		return;
	}

	if ((s_view_page < 1U) || (s_view_page > count)) {
		next = 1U;
	} else if (s_view_page >= count) {
		next = 1U;
	} else {
		next = (uint8_t)(s_view_page + 1U);
	}
	(void)app_view_show(s_view_mode, next);
}

/** R：home → notes@1 → info@1 → home */
static void app_view_mode_next(void)
{
	uint8_t mode = s_view_mode;
	uint8_t page;

	if (mode == PAGE_SLOT_MODE_HOME) {
		mode = PAGE_SLOT_MODE_NOTES;
	} else if (mode == PAGE_SLOT_MODE_NOTES) {
		mode = PAGE_SLOT_MODE_INFO;
	} else {
		mode = PAGE_SLOT_MODE_HOME;
	}
	page = (mode == PAGE_SLOT_MODE_HOME) ? 0U : 1U;
	(void)app_view_show(mode, page);
}

static status_t app_slot_status_send(void)
{
	uint8_t pl[BLE_CMD_SLOT_STATUS_PAYLOAD_SIZE];
	uint8_t notes_title[PAGE_SLOT_TITLE_BUF];
	uint8_t info_title[PAGE_SLOT_TITLE_BUF];
	uint8_t notes_count = 0U;
	uint8_t info_count = 0U;
	uint8_t notes_tlen = 0U;
	uint8_t info_tlen = 0U;
	status_t st;

	memset(pl, 0, sizeof(pl));
	memset(notes_title, 0, sizeof(notes_title));
	memset(info_title, 0, sizeof(info_title));

	(void)page_slot_group_get(PAGE_SLOT_MODE_NOTES, &notes_count, notes_title, &notes_tlen);
	(void)page_slot_group_get(PAGE_SLOT_MODE_INFO, &info_count, info_title, &info_tlen);

	pl[0] = s_view_mode;
	pl[1] = s_view_page;
	pl[2] = page_slot_home_valid() ? 1U : 0U;
	pl[3] = notes_count;
	pl[4] = notes_tlen;
	memcpy(&pl[5], notes_title, PAGE_SLOT_TITLE_BUF);
	pl[37] = info_count;
	pl[38] = info_tlen;
	memcpy(&pl[39], info_title, PAGE_SLOT_TITLE_BUF);
	pl[71] = page_slot_notes_valid_bits();
	pl[72] = page_slot_info_valid_bits();

	st = ble_cmd_send(BLE_CMD_SLOT_STATUS, pl, sizeof(pl));
	if (st == STATUS_OK) {
		printk("app: SLOT_STATUS mode=%u page=%u notes=%u info=%u\n",
		       (unsigned)s_view_mode, (unsigned)s_view_page, (unsigned)notes_count,
		       (unsigned)info_count);
	}
	return st;
}
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
typedef enum {
	APP_DISP_IDLE = 0,
	APP_DISP_RECEIVING,
} app_disp_state_t;

typedef struct {
	app_disp_state_t state;
	uint8_t planes;
	uint8_t flags;
	uint8_t mode;
	uint8_t page;
	uint16_t bw_len;
	uint16_t red_len;
	uint32_t bw_crc;
	uint32_t red_crc;
} app_disp_session_t;

static app_disp_session_t s_disp;

static void app_disp_reset(void)
{
	memset(&s_disp, 0, sizeof(s_disp));
	s_disp.state = APP_DISP_IDLE;
}
#endif

static void app_ack(uint16_t cmd_id, status_t st)
{
	status_t send_st = ble_cmd_send_ack(cmd_id, st);
	bool noisy_ok = (cmd_id == BLE_CMD_DISP_CHUNK) && (st == STATUS_OK);

	if (!noisy_ok && (st != STATUS_OK)) {
		printk("app: ACK cmd=%u st=%d\n", (unsigned)cmd_id, (int)st);
	}

	if (send_st == STATUS_INVALID_STATE) {
		printk("app: ACK skipped (enable TX Notify)\n");
	} else if (send_st != STATUS_OK) {
		printk("app: ACK send failed %d\n", (int)send_st);
	}
}

static void app_ble_evt_cb(const ble_evt_t *evt, void *user)
{
	ARG_UNUSED(user);

	if (evt == NULL) {
		return;
	}

	if (evt->id == BLE_EVT_CONNECTED) {
		printk("ble: connected\n");
#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
		atomic_set(&s_slot_status_pending, 1);
#endif
		k_event_post(&app_events, APP_EVT_BLE_GAP);
	} else if (evt->id == BLE_EVT_DISCONNECTED) {
		printk("ble: disconnected\n");
#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
		atomic_set(&s_slot_status_pending, 0);
#endif
		k_event_post(&app_events, APP_EVT_BLE_GAP);
	}
}

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
static status_t app_disp_handle_begin(const uint8_t *payload, uint16_t plen)
{
	uint16_t width;
	uint16_t height;
	uint8_t planes;
	uint8_t flags;
	uint8_t mode;
	uint8_t page;
	uint16_t bw_len;
	uint16_t red_len;
	uint32_t bw_crc;
	uint32_t red_crc;
	uint32_t plane_sz;
	status_t st;

	/* β：固定 20B；不解析旧 18B */
	if (plen != BLE_CMD_DISP_BEGIN_PAYLOAD_SIZE_EXT) {
		printk("app: BEGIN plen=%u want=%u\n", (unsigned)plen,
		       (unsigned)BLE_CMD_DISP_BEGIN_PAYLOAD_SIZE_EXT);
		return STATUS_INVALID_ARG;
	}
	if (!epd_board_is_ready()) {
		return STATUS_INVALID_STATE;
	}
	if (epd_board_planes_busy()) {
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
	mode = payload[18];
	page = payload[19];

	plane_sz = epd_board_plane_size();

	if ((width != epd_board_width()) || (height != epd_board_height())) {
		printk("app: BEGIN size mismatch %ux%u (board %ux%u)\n", width, height,
		       epd_board_width(), epd_board_height());
		return STATUS_INVALID_ARG;
	}
	if ((flags & (uint8_t)~BLE_CMD_DISP_FLAG_SHOW) != 0U) {
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

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	/* notes/info page0 禁止上传（组 meta 走 4001） */
	if (!page_slot_is_content_page(mode, page)) {
		printk("app: BEGIN reject non-content mode=%u page=%u\n", (unsigned)mode,
		       (unsigned)page);
		return STATUS_INVALID_ARG;
	}
#else
	if ((mode != 0U) || (page != 0U)) {
		return STATUS_NOT_SUPPORTED;
	}
#endif

	st = epd_board_clear_planes(EPD_BOARD_PLANES_BW | EPD_BOARD_PLANES_RED);
	if (st != STATUS_OK) {
		return st;
	}

	s_disp.state = APP_DISP_RECEIVING;
	s_disp.planes = planes;
	s_disp.flags = flags;
	s_disp.mode = mode;
	s_disp.page = page;
	s_disp.bw_len = bw_len;
	s_disp.red_len = red_len;
	s_disp.bw_crc = bw_crc;
	s_disp.red_crc = red_crc;

	printk("app: BEGIN mode=%u page=%u show=%u\n", (unsigned)mode, (unsigned)page,
	       (unsigned)((flags & BLE_CMD_DISP_FLAG_SHOW) != 0U));
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
	if (epd_board_planes_busy()) {
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

	return epd_board_write_plane(plane, offset, &payload[BLE_CMD_DISP_CHUNK_HDR_SIZE],
				     data_len);
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
			printk("app: BW CRC fail got=0x%08x expect=0x%08x\n", got, s_disp.bw_crc);
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
			printk("app: RED CRC fail got=0x%08x expect=0x%08x\n", got, s_disp.red_crc);
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
	if (epd_board_planes_busy()) {
		return STATUS_INVALID_STATE;
	}

	st = app_disp_verify_crc();
	if (st != STATUS_OK) {
		printk("app: REFRESH CRC fail st=%d\n", (int)st);
		app_disp_reset();
		return st;
	}

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	{
		bool show = (s_disp.flags & BLE_CMD_DISP_FLAG_SHOW) != 0U;
		uint8_t mode = s_disp.mode;
		uint8_t page = s_disp.page;

		st = epd_board_store_refresh_request(mode, page, show);
		if (st == STATUS_OK) {
			printk("app: REFRESH queued mode=%u page=%u show=%u\n",
			       (unsigned)mode, (unsigned)page, (unsigned)show);
			if (show) {
				app_view_set(mode, page);
			}
		}
	}
#else
	st = epd_board_refresh_request();
#endif
	app_disp_reset();
	return st;
}
#endif /* CONFIG_INK_PANEL_EPD */

#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
static status_t app_handle_get_slot_status(uint16_t plen)
{
	if (plen != 0U) {
		return STATUS_INVALID_ARG;
	}
	return app_slot_status_send();
}

static status_t app_handle_set_group_meta(const uint8_t *payload, uint16_t plen)
{
	uint8_t mode;
	uint8_t count;
	uint8_t title_len;
	status_t st;

	if (plen != BLE_CMD_SET_GROUP_META_PAYLOAD_SIZE) {
		return STATUS_INVALID_ARG;
	}
	mode = payload[0];
	count = payload[1];
	title_len = payload[2];
	if (title_len > BLE_CMD_GROUP_TITLE_MAX) {
		return STATUS_INVALID_ARG;
	}

	st = page_slot_group_set(mode, count, &payload[3], title_len);
	if (st == STATUS_OK) {
		(void)app_slot_status_send();
	}
	return st;
}

static void app_try_slot_status_pending(void)
{
	status_t st;

	if (atomic_get(&s_slot_status_pending) == 0) {
		return;
	}
	if (!ble_is_connected()) {
		atomic_set(&s_slot_status_pending, 0);
		return;
	}

	st = app_slot_status_send();
	if (st == STATUS_OK) {
		atomic_set(&s_slot_status_pending, 0);
	}
	/* STATUS_INVALID_STATE：Notify 未开，保持 pending 下次再试 */
}
#endif

static void app_handle_ble_cmds(void)
{
	ble_cmd_msg_t msg;
	status_t st;

	while (ble_cmd_fetch(&msg)) {
		switch (msg.cmd_id) {
		case BLE_CMD_PING:
			app_ack(BLE_CMD_PING, STATUS_OK);
			break;

#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
		case BLE_CMD_GET_SLOT_STATUS:
			st = app_handle_get_slot_status(msg.payload_len);
			app_ack(BLE_CMD_GET_SLOT_STATUS, st);
			break;

		case BLE_CMD_SET_GROUP_META:
			st = app_handle_set_group_meta(msg.payload, msg.payload_len);
			app_ack(BLE_CMD_SET_GROUP_META, st);
			break;

		case BLE_CMD_SET_HOME_OVERLAY:
			if (msg.payload_len != 1U) {
				st = STATUS_INVALID_ARG;
			} else {
				st = page_slot_home_overlay_set(msg.payload[0]);
				if (st == STATUS_OK) {
					/* 保存开关后立刻重显首页，墨水屏可见叠层 */
					st = epd_board_show_slot_request(PAGE_SLOT_MODE_HOME, 0U);
					if (st == STATUS_OK) {
						app_view_set(PAGE_SLOT_MODE_HOME, 0U);
					}
				}
			}
			app_ack(BLE_CMD_SET_HOME_OVERLAY, st);
			break;

		case BLE_CMD_DELETE_LAST_PAGE:
			if (msg.payload_len != 1U) {
				st = STATUS_INVALID_ARG;
			} else {
				st = page_slot_group_delete_last(msg.payload[0]);
				if (st == STATUS_OK) {
					(void)app_slot_status_send();
				}
			}
			app_ack(BLE_CMD_DELETE_LAST_PAGE, st);
			break;
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
		case BLE_CMD_DISPLAY:
			st = epd_board_demo_request();
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
#endif

		default:
			printk("app: unknown cmd_id=%u\n", (unsigned)msg.cmd_id);
			app_ack(msg.cmd_id, STATUS_NOT_SUPPORTED);
			break;
		}
	}
}

static void app_ble_heartbeat_work_fn(struct k_work *work)
{
	uint8_t flags = 0U;
	status_t st;

	ARG_UNUSED(work);

	if (ble_is_connected()) {
		if (ble_is_advertising()) {
			flags |= APP_HB_FLAG_ADV;
		}
		flags |= APP_HB_FLAG_CONN;

		st = ble_cmd_send_heartbeat(s_hb_seq, flags);
		if ((st != STATUS_OK) && (st != STATUS_INVALID_STATE)) {
			printk("app: HEARTBEAT fail st=%d\n", (int)st);
		}
		s_hb_seq++;
	}

	(void)k_work_reschedule(&s_hb_work, K_MSEC(APP_HB_PERIOD_MS));
}

static void app_ble_heartbeat_start(void)
{
	k_work_init_delayable(&s_hb_work, app_ble_heartbeat_work_fn);
	(void)k_work_schedule(&s_hb_work, K_MSEC(APP_HB_PERIOD_MS));
}

static void app_handle_button(void)
{
	btn_id_e id;
	btn_event_e ev;

	if (!app_event_take(APP_EVT_BUTTON)) {
		return;
	}

	button_last_event_get(&id, &ev);
	button_last_event_clear();

	if (id != BTN_ID_CONFIRM) {
		return;
	}

	if (ev == BTN_EVENT_SINGLE_CLICK) {
		status_t st;

		if (ble_is_connected()) {
			st = ble_disconnect();
			if (st != STATUS_OK) {
				printk("app: BLE disconnect st=%d\n", (int)st);
			}
			/* restart_on_disconnect=true → 断后自动再广播 */
		} else {
			st = ble_adv_start();
			if (st != STATUS_OK) {
				printk("app: BLE adv_start st=%d\n", (int)st);
			}
		}
		return;
	}

	if (ev == BTN_EVENT_LONG_PRESS) {
		if (!nvs_boot_mode_set(NVS_BOOT_MODE_FACTORY)) {
			printk("app: factory boot_mode set fail\n");
			return;
		}
		k_msleep(50);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

#if IS_ENABLED(CONFIG_INK_PANEL_CAP1203)
static void app_handle_touch(void)
{
	uint8_t nav;

	if (!app_event_take(APP_EVT_TOUCH)) {
		return;
	}

	nav = cap1203_board_nav_take();
	if (nav == 0U) {
		return;
	}

	printk("app: CAP click%s%s\n",
	       ((nav & CAP1203_NAV_L) != 0U) ? " L" : "",
	       ((nav & CAP1203_NAV_R) != 0U) ? " R" : "");

#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	/* 同时按下时优先切模 */
	if ((nav & CAP1203_NAV_R) != 0U) {
		app_view_mode_next();
	} else if ((nav & CAP1203_NAV_L) != 0U) {
		app_view_page_next();
	}
#else
	ARG_UNUSED(nav);
#endif
}
#endif

static void app_poll_ble_events(void)
{
	uint32_t ev = k_event_wait(&app_events, APP_EVT_BLE_GAP | APP_EVT_BLE_CMD, false,
				   K_NO_WAIT);

	if ((ev & APP_EVT_BLE_GAP) != 0U) {
		k_event_clear(&app_events, APP_EVT_BLE_GAP);
	}
	if ((ev & APP_EVT_BLE_CMD) != 0U) {
		k_event_clear(&app_events, APP_EVT_BLE_CMD);
		app_handle_ble_cmds();
	}
#if IS_ENABLED(CONFIG_INK_PANEL_EPD) && IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	app_try_slot_status_pending();
#endif
}

/** main 环一拍：按键 / 触摸 / BLE CMD（不阻塞；心跳走独立 work） */
static void app_poll(void)
{
	app_handle_button();
#if IS_ENABLED(CONFIG_INK_PANEL_CAP1203)
	{
		static uint8_t s_cap_poll_div;

		app_handle_touch();
		/* ~100ms 轮询兜底（APP_LOOP_MS=20） */
		if (++s_cap_poll_div >= 5U) {
			s_cap_poll_div = 0U;
			cap1203_board_poll();
		}
	}
#endif
	app_poll_ble_events();
}

status_t app_init(void)
{
	status_t st;

	printk("ink_panel boot\n");
	button_long_press_ms_set_id(BTN_ID_CONFIRM, 5000U);

	if (npm1300_board_bringup() != 0) {
		printk("npm1300 bringup fail\n");
	}

#if IS_ENABLED(CONFIG_INK_PANEL_CAP1203)
	if (cap1203_board_bringup() != 0) {
		printk("cap1203 bringup fail\n");
	}
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	if (w25q_board_init() != 0) {
		printk("w25q init fail\n");
	} else if (page_slot_init() != STATUS_OK) {
		printk("page_slot init fail\n");
	}
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
	app_disp_reset();
#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	app_view_set(PAGE_SLOT_MODE_HOME, 0U);
#endif
	epd_board_start();
#endif

	ble_evt_set_callback(app_ble_evt_cb, NULL);
	ble_adv_set_restart_on_disconnect(true);

	st = ble_init();
	if (st != STATUS_OK) {
		printk("ble_init failed: %d\n", (int)st);
		return STATUS_FAIL;
	}

	app_ble_heartbeat_start();

	printk("ink_panel ok board=%s\n", board_config_get()->name);
	return STATUS_OK;
}

void app_run(void)
{
	while (1) {
		start_poll();
		app_poll();
		k_msleep(APP_LOOP_MS);
	}
}
