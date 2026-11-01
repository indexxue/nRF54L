/*
 * zigbee_ed — LED/按键 + Zigbee ED + OnOff Server；无 BLE/EPD。
 *
 * BTN1 短按 → 60s 配网窗 + Steering；
 * BTN2 双击 → leave；BTN2 长按 3s → 擦 ZBOSS NVRAM 并重启。
 * 网关 znp light → EP1 OnOff → 板载绿灯 + light: on|off
 */

#include "start.h"
#include "zigbee_app.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "board.h"
#include "button.h"
#include "device_profile.h"
#include "event.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"

static zigbee_app_state_t s_zb_state = ZIGBEE_APP_STATE_IDLE;
static bool s_light_on;

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

static void app_light_apply(bool on)
{
	s_light_on = on;
	app_zb_led_cancel_all();
	/* 控灯用绿灯（LED0）；灭时两灯都关 */
	led_scene_led_direct_set(LED_SCENE_LED_0, on);
	led_scene_led_direct_set(LED_SCENE_LED_1, false);
	LOG_INFO("light: %s", on ? "on" : "off");
	printk("light: %s\n", on ? "on" : "off");
}

static void app_zb_log_net(const zigbee_app_evt_t *evt)
{
	switch (evt->state) {
	case ZIGBEE_APP_STATE_STEERING:
		LOG_INFO("zigbee: steering");
		printk("zigbee: steering (mask=0x%08x ep=%d)\n",
		       CONFIG_ZIGBEE_CHANNEL_MASK, ZIGBEE_APP_ENDPOINT);
		break;
	case ZIGBEE_APP_STATE_REJOINING:
		LOG_INFO("zigbee: rejoining");
		printk("zigbee: rejoining (same-net)\n");
		break;
	case ZIGBEE_APP_STATE_JOINED:
		if (evt->ieee_valid) {
			/* 0x0000=协调器；否则对照 Router 串口 joined 的 nwk= */
			const char *parent_role =
				(evt->parent_nwk == 0x0000U) ? "coord" : "non-coord";

			LOG_INFO(
				"zigbee: joined nwk=0x%04x parent=0x%04x(%s) ieee=%02X%02X%02X%02X%02X%02X%02X%02X",
				evt->nwk_addr, evt->parent_nwk, parent_role, evt->ieee[7],
				evt->ieee[6], evt->ieee[5], evt->ieee[4], evt->ieee[3],
				evt->ieee[2], evt->ieee[1], evt->ieee[0]);
			printk("zigbee: joined nwk=0x%04x parent=0x%04x(%s) ieee=%02X%02X%02X%02X%02X%02X%02X%02X\n",
			       evt->nwk_addr, evt->parent_nwk, parent_role, evt->ieee[7],
			       evt->ieee[6], evt->ieee[5], evt->ieee[4], evt->ieee[3],
			       evt->ieee[2], evt->ieee[1], evt->ieee[0]);
		} else {
			LOG_INFO("zigbee: joined");
			printk("zigbee: joined\n");
		}
		break;
	case ZIGBEE_APP_STATE_FAILED:
		LOG_INFO("zigbee: failed");
		printk("zigbee: failed\n");
		break;
	case ZIGBEE_APP_STATE_IDLE:
	default:
		LOG_INFO("zigbee: idle / left");
		printk("zigbee: idle / left\n");
		break;
	}
}

static void app_handle_zigbee_evt(void)
{
	zigbee_app_evt_t evt;

	while (zigbee_app_evt_fetch(&evt)) {
		if (evt.kind == ZIGBEE_APP_EVT_LIGHT) {
			app_light_apply(evt.light_on);
			continue;
		}

		s_zb_state = evt.state;
		app_zb_log_net(&evt);
		app_zb_led_apply(evt.state);
		if (evt.state == ZIGBEE_APP_STATE_IDLE) {
			s_light_on = false;
		}
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
		LOG_INFO("app: BTN1 short -> steer");
		zigbee_app_steer_request();
		return;
	}

	if ((id == BTN_ID_BACK) && (ev == BTN_EVENT_DOUBLE_CLICK)) {
		LOG_INFO("app: BTN2 double -> leave");
		s_zb_state = ZIGBEE_APP_STATE_IDLE;
		s_light_on = false;
		app_zb_led_apply(ZIGBEE_APP_STATE_IDLE);
		zigbee_app_leave_request();
		return;
	}

	if ((id == BTN_ID_BACK) && (ev == BTN_EVENT_LONG_PRESS)) {
		LOG_INFO("app: BTN2 long 3s -> erase NVRAM + reboot");
		printk("app: BTN2 long -> erase NVRAM + reboot\n");
		zigbee_app_nv_erase_reboot_request();
	}
}

status_t app_init(void)
{
	uint8_t bmode = nvs_boot_mode_get();
	status_t st;

	/* 尽早打串口，便于确认已越过 start_boot / HFXO */
	printk("zigbee_ed boot\n");

	/* BTN2 长按清 NV：门限 3s（common 默认 10s） */
	button_long_press_ms_set(3000U);

	LOG_INFO("zigbee_ed app_init hw=%s bmode=%u mask=0x%08x ep=%d",
		 device_profile_hardware_name(device_profile_hardware_id()), (unsigned)bmode,
		 CONFIG_ZIGBEE_CHANNEL_MASK, ZIGBEE_APP_ENDPOINT);
	printk("zigbee_ed ok mask=0x%08x ep=%d (BTN1=steer BTN2-dbl=leave BTN2-long3s=eraseNV)\n",
	       CONFIG_ZIGBEE_CHANNEL_MASK, ZIGBEE_APP_ENDPOINT);

	st = zigbee_app_start();
	if (st != STATUS_OK) {
		LOG_ERROR("zigbee_app_start failed st=%d", (int)st);
		return st;
	}

	s_light_on = false;
	app_zb_led_apply(ZIGBEE_APP_STATE_IDLE);
	return STATUS_OK;
}

void app_run(void)
{
	LOG_INFO("zigbee_ed run (start_poll 20ms)");
	(void)s_zb_state;
	(void)s_light_on;

	while (1) {
		start_poll();
		app_handle_button();
		app_handle_zigbee_evt();
		k_sleep(K_MSEC(20));
	}
}
