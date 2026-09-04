/*
 * eink_tag — 当前：UART + LED 场景 + 双按键（内部 RC；NFC/BLE/EPD 暂关）
 */

#include "app.h"

#include <zephyr/kernel.h>

#include "board.h"
#include "button.h"
#include "device_profile.h"
#include "event.h"
#include "fw_version.h"
#include "input.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"

status_t app_startup(void)
{
	const board_config_t *board;
	status_t st;

	if (log_init(NULL) != STATUS_OK) {
		return STATUS_FAIL;
	}
	(void)log_set_level(LOG_LEVEL_INFO);

	event_init();
	nvs_init();

	if (board_init() != STATUS_OK) {
		LOG_ERROR("board_init failed");
		return STATUS_FAIL;
	}

	board = board_config_get();
	LOG_INFO("eink_tag start ver=%s product=%s board=%s", version_get_string(),
		 device_profile_product()->name, board->name);
	LOG_INFO("soc uart=%u led=%u btn=%u spi=%u ble=%d nfc=%d | LFCLK=RC",
		 board_uart_count(), board_led_count(), board_button_count(),
		 board_spi_bus_count(), board_wants(BOARD_MASK_BLE) ? 1 : 0,
		 board_wants(BOARD_MASK_NFC) ? 1 : 0);
	LOG_INFO("pins UART P1.09/P1.10 | LED_G P1.05 LED_B P1.04 | BTN1 P0.00 BTN2 P0.01");

	if (board_wants(BOARD_MASK_LED)) {
		led_scene_run(LED_SCENE_ID_BOOTUP);
	}

	st = button_init(button_log_notify);
	if (st == STATUS_OK) {
		LOG_INFO("button ready (btn1/btn2)");
	} else if (st != STATUS_NOT_SUPPORTED) {
		LOG_WARN("button_init failed: %d", (int)st);
	}

	st = input_init(input_log_notify);
	if (st == STATUS_OK) {
		LOG_INFO("input ready");
	} else if (st != STATUS_NOT_SUPPORTED) {
		LOG_WARN("input_init failed: %d", (int)st);
	}

	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("eink_tag run (led_scene + button 20ms, heartbeat 1s)");

	while (1) {
		led_scene_update();
		button_schedule();
		input_schedule();
		if ((tick % 50U) == 0U) {
			LOG_INFO("heartbeat tick=%u", tick / 50U);
		}
		tick++;
		k_sleep(K_MSEC(20));
	}
}
