/*
 * ink_panel — nPM/CAP1203 + Zigbee ED（HFXO/LFXO）。
 * BTN1 短按配网；长按 5s → factory boot_mode。
 */

#include "start.h"
#include "zigbee_app.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "board.h"
#include "button.h"
#include "cap1203_board.h"
#include "event.h"
#include "led_scene.h"
#include "npm1300_board.h"
#include "nvs.h"

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
#include "epd_board.h"
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_NFC)
#include "nfc_board.h"
#endif

static zigbee_app_state_t s_zb_state = ZIGBEE_APP_STATE_IDLE;

static void app_zb_led_cancel_all(void)
{
	led_scene_cancel(LED_SCENE_ID_PAIRING);
	led_scene_cancel(LED_SCENE_ID_SUCCESS);
	led_scene_cancel(LED_SCENE_ID_ERROR);
	led_scene_cancel(LED_SCENE_ID_TRIGGER);
}

static void app_zb_led_apply(zigbee_app_state_t st)
{
	app_zb_led_cancel_all();

	switch (st) {
	case ZIGBEE_APP_STATE_STEERING:
	case ZIGBEE_APP_STATE_REJOINING:
		led_scene_run(LED_SCENE_ID_PAIRING);
		break;
	case ZIGBEE_APP_STATE_JOINED:
		led_scene_run(LED_SCENE_ID_SUCCESS);
		break;
	case ZIGBEE_APP_STATE_FAILED:
		led_scene_run(LED_SCENE_ID_ERROR);
		break;
	case ZIGBEE_APP_STATE_IDLE:
	default:
		led_scene_run(LED_SCENE_ID_TRIGGER);
		break;
	}
}

static void app_handle_zigbee_evt(void)
{
	zigbee_app_evt_t evt;

	while (zigbee_app_evt_fetch(&evt)) {
		if (evt.kind == ZIGBEE_APP_EVT_LIGHT) {
			led_scene_led_direct_set(LED_SCENE_LED_0, evt.light_on);
			printk("light: %s\n", evt.light_on ? "on" : "off");
			continue;
		}
		if (evt.kind != ZIGBEE_APP_EVT_NET) {
			continue;
		}
		s_zb_state = evt.state;
		switch (evt.state) {
		case ZIGBEE_APP_STATE_STEERING:
			printk("zigbee: steering\n");
			break;
		case ZIGBEE_APP_STATE_REJOINING:
			printk("zigbee: rejoining\n");
			break;
		case ZIGBEE_APP_STATE_JOINED:
			printk("zigbee: joined nwk=0x%04x\n", evt.nwk_addr);
			break;
		case ZIGBEE_APP_STATE_FAILED:
			printk("zigbee: failed\n");
			break;
		case ZIGBEE_APP_STATE_IDLE:
		default:
			printk("zigbee: idle\n");
			break;
		}
		app_zb_led_apply(evt.state);
	}
}

static void app_handle_button(void)
{
	btn_id_e id;
	btn_event_e ev;

	if (!event_is_set(EVT_ID_BUTTON)) {
		return;
	}

	button_last_event_get(&id, &ev);
	event_clear(EVT_ID_BUTTON);
	button_last_event_clear();

	if ((id == BTN_ID_CONFIRM) && (ev == BTN_EVENT_SINGLE_CLICK)) {
		printk("app: BTN1 short -> steer\n");
		zigbee_app_steer_request();
		return;
	}

	if ((id == BTN_ID_CONFIRM) && (ev == BTN_EVENT_LONG_PRESS)) {
		printk("app: BTN1 long -> factory reboot\n");
		if (!nvs_boot_mode_set(NVS_BOOT_MODE_FACTORY)) {
			printk("app: factory boot_mode set fail\n");
			return;
		}
		k_msleep(50);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

status_t app_init(void)
{
	status_t st;

	printk("ink_panel boot (hfxo+lfxo+zigbee)\n");

	button_long_press_ms_set_id(BTN_ID_CONFIRM, 5000U);

	if (npm1300_board_bringup() != 0) {
		printk("npm1300 bringup fail\n");
	}
	if (cap1203_board_bringup() != 0) {
		printk("cap1203 bringup fail\n");
	}
#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
	/* bringup+全刷在 epd_smoke 线程，避免堵死 Zigbee/LED */
	epd_board_smoke_start();
#else
	printk("epd: disabled (CONFIG_INK_PANEL_EPD=n)\n");
#endif

#if IS_ENABLED(CONFIG_INK_PANEL_NFC)
	if (nfc_board_bringup() != STATUS_OK) {
		printk("nfc: skip (bringup failed)\n");
	}
#else
	printk("nfc: disabled (CONFIG_INK_PANEL_NFC=n)\n");
#endif

	st = zigbee_app_start();
	if (st != STATUS_OK) {
		printk("zigbee_app_start fail st=%d\n", (int)st);
		return st;
	}

	app_zb_led_apply(ZIGBEE_APP_STATE_IDLE);
	printk("ink_panel ok mask=0x%08x (BTN1=steer)\n", CONFIG_ZIGBEE_CHANNEL_MASK);
	return STATUS_OK;
}

void app_run(void)
{
	(void)s_zb_state;

	while (1) {
		start_poll();
		app_handle_button();
		app_handle_zigbee_evt();
#if IS_ENABLED(CONFIG_INK_PANEL_NFC)
		nfc_board_poll();
#endif
		k_msleep(20);
	}
}
