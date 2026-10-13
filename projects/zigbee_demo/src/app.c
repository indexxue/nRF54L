/*
 * zigbee_demo ? LED/?? + Zigbee ED ?????? BLE/EPD??
 *
 * BTN1 ?? ? Network Steering?BTN2 ?? ? leave / ?????????
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

static void app_zb_led_apply(zigbee_app_state_t st)
{
	led_scene_cancel(LED_SCENE_ID_PAIRING);
	led_scene_cancel(LED_SCENE_ID_SUCCESS);
	led_scene_cancel(LED_SCENE_ID_ERROR);

	switch (st) {
	case ZIGBEE_APP_STATE_STEERING:
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
		led_scene_led_direct_set(LED_SCENE_LED_0, false);
		led_scene_led_direct_set(LED_SCENE_LED_1, false);
		break;
	}
}

static void app_zb_log_evt(const zigbee_app_evt_t *evt)
{
	switch (evt->state) {
	case ZIGBEE_APP_STATE_STEERING:
		LOG_INFO("zigbee: steering");
		printk("zigbee: steering (ch=%d)\n", CONFIG_ZIGBEE_CHANNEL);
		break;
	case ZIGBEE_APP_STATE_JOINED:
		if (evt->ieee_valid) {
			LOG_INFO(
				"zigbee: joined nwk=0x%04x ieee=%02X%02X%02X%02X%02X%02X%02X%02X",
				evt->nwk_addr, evt->ieee[7], evt->ieee[6], evt->ieee[5],
				evt->ieee[4], evt->ieee[3], evt->ieee[2], evt->ieee[1],
				evt->ieee[0]);
			printk("zigbee: joined nwk=0x%04x ieee=%02X%02X%02X%02X%02X%02X%02X%02X\n",
			       evt->nwk_addr, evt->ieee[7], evt->ieee[6], evt->ieee[5],
			       evt->ieee[4], evt->ieee[3], evt->ieee[2], evt->ieee[1],
			       evt->ieee[0]);
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
		s_zb_state = evt.state;
		app_zb_log_evt(&evt);
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
		LOG_INFO("app: BTN1 short -> steer");
		zigbee_app_steer_request();
		return;
	}

	if ((id == BTN_ID_BACK) && (ev == BTN_EVENT_LONG_PRESS)) {
		LOG_INFO("app: BTN2 long -> leave");
		zigbee_app_leave_request();
	}
}

status_t app_init(void)
{
	uint8_t bmode = nvs_boot_mode_get();
	status_t st;

	LOG_INFO("zigbee_demo app_init hw=%s bmode=%u ch=%d",
		 device_profile_hardware_name(device_profile_hardware_id()), (unsigned)bmode,
		 CONFIG_ZIGBEE_CHANNEL);
	printk("zigbee_demo printk ok ch=%d (BTN1=steer BTN2-long=leave)\n", CONFIG_ZIGBEE_CHANNEL);

	st = zigbee_app_start();
	if (st != STATUS_OK) {
		LOG_ERROR("zigbee_app_start failed st=%d", (int)st);
		return st;
	}

	app_zb_led_apply(ZIGBEE_APP_STATE_IDLE);
	return STATUS_OK;
}

void app_run(void)
{
	LOG_INFO("zigbee_demo run (start_poll 20ms)");

	while (1) {
		start_poll();
		app_handle_button();
		app_handle_zigbee_evt();
		k_sleep(K_MSEC(20));
	}
}
