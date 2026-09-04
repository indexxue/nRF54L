/*
 * eink_tag — UART + LED + 按键 + 2.9" BWR EPD 冒烟（内部 RC；NFC/BLE 暂关）
 */

#include "app.h"

#include <zephyr/kernel.h>

#include "board.h"
#include "button.h"
#include "device_profile.h"
#include "epd_board.h"
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
	LOG_INFO("soc uart=%u led=%u btn=%u spi=%u/%u ble=%d nfc=%d | LFCLK=RC",
		 board_uart_count(), board_led_count(), board_button_count(),
		 board_spi_bus_count(), board_spi_slave_count(),
		 board_wants(BOARD_MASK_BLE) ? 1 : 0, board_wants(BOARD_MASK_NFC) ? 1 : 0);
	LOG_INFO("pins UART P1.09/P1.10 | LED_G P1.05 LED_B P1.04 | BTN P0.00/P0.01");
	LOG_INFO("epd SPI00 SCK=P2.01 MOSI=P2.04 CS=P2.05 | DC=P1.08 RST=P1.07 BUSY=P1.06");

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

	/* 先 SPI/bitbang 自检，再 init + 刷屏 */
	st = epd_board_spi_selftest();
	if (st != STATUS_OK) {
		LOG_ERROR("epd_board_spi_selftest failed: %d", (int)st);
		return STATUS_FAIL;
	}

	st = epd_board_init();
	if (st != STATUS_OK) {
		LOG_ERROR("epd_board_init failed: %d", (int)st);
		return STATUS_FAIL;
	}

	st = epd_board_test_run();
	if (st != STATUS_OK) {
		LOG_ERROR("epd_board_test_run failed: %d", (int)st);
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("eink_tag run (epd tested; led_scene + button 20ms, heartbeat 1s)");

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
