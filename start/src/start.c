/*
 * Shared start_boot / start_poll for all apps.
 */

#include "start.h"

#include <zephyr/kernel.h>

#if defined(CONFIG_START_REQUEST_HFXO) && CONFIG_START_REQUEST_HFXO
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/onoff.h>
#endif

#include "board.h"
#include "button.h"
#include "device_profile.h"
#include "event.h"
#include "fw_version.h"
#include "input.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"

#if defined(CONFIG_START_REQUEST_HFXO) && CONFIG_START_REQUEST_HFXO
/* Keep HFXO onoff request alive so HF does not drop back to HFINT. */
static struct onoff_client s_hfxo_cli;

static status_t hfxo_start(void)
{
	struct onoff_manager *mgr;
	int err;
	int res;

	mgr = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
	if (mgr == NULL) {
		LOG_ERROR("hfxo: no clock manager");
		return STATUS_FAIL;
	}

	sys_notify_init_spinwait(&s_hfxo_cli.notify);
	err = onoff_request(mgr, &s_hfxo_cli);
	if (err < 0) {
		LOG_ERROR("hfxo: request failed %d", err);
		return STATUS_FAIL;
	}

	do {
		err = sys_notify_fetch_result(&s_hfxo_cli.notify, &res);
		if ((err == 0) && (res != 0)) {
			LOG_ERROR("hfxo: start failed %d", res);
			return STATUS_FAIL;
		}
	} while (err != 0);

	LOG_INFO("hfxo: 32M crystal running");
	return STATUS_OK;
}
#endif /* CONFIG_START_REQUEST_HFXO */

status_t start_boot(void)
{
	status_t st;
	const board_config_t *board;

	if (log_init(NULL) != STATUS_OK) {
		return STATUS_FAIL;
	}
	(void)log_set_level(LOG_LEVEL_INFO);

	event_init();
	nvs_init();
	/* nvs_init 末尾已 nvs_log_boot_dump() */

	if (board_init() != STATUS_OK) {
		LOG_WARN("board_init failed (board=%s)", board_config_get()->name);
	}

#if defined(CONFIG_START_REQUEST_HFXO) && CONFIG_START_REQUEST_HFXO
	st = hfxo_start();
	if (st != STATUS_OK) {
		return st;
	}
#endif

	board = board_config_get();
	LOG_INFO("start ver=%s product=%s board=%s", version_get_string(),
		 device_profile_product()->name, board->name);
	LOG_INFO("mask=0x%x uart=%u led=%u btn=%u spi=%u/%u ble=%d nfc=%d",
		 board->mask, board_uart_count(), board_led_count(), board_button_count(),
		 board_spi_bus_count(), board_spi_slave_count(),
		 board_wants(BOARD_MASK_BLE) ? 1 : 0, board_wants(BOARD_MASK_NFC) ? 1 : 0);

	if (board_wants(BOARD_MASK_LED)) {
		led_scene_run(LED_SCENE_ID_BOOTUP);
	}

	if (board_wants(BOARD_MASK_BUTTON)) {
		st = button_init(button_log_notify);
		if (st == STATUS_OK) {
			LOG_INFO("button ready");
		} else if (st != STATUS_NOT_SUPPORTED) {
			LOG_WARN("button_init failed: %d", (int)st);
		}
	}

	if (board_wants(BOARD_MASK_INPUT) || board_wants(BOARD_MASK_IRQ)) {
		st = input_init(input_log_notify);
		if (st == STATUS_OK) {
			LOG_INFO("input ready");
		} else if (st != STATUS_NOT_SUPPORTED) {
			LOG_WARN("input_init failed: %d", (int)st);
		}
	}

	return STATUS_OK;
}

void start_poll(void)
{
	led_scene_update();
	button_schedule();
	input_schedule();
}
