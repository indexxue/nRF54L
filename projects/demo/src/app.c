/*
 * demo — board_init + led_scene bootup blink
 */

#include "app.h"

#include <zephyr/kernel.h>

#include "board.h"
#include "device_profile.h"
#include "event.h"
#include "fw_version.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"

status_t app_startup(void)
{
	if (log_init(NULL) != STATUS_OK) {
		return STATUS_FAIL;
	}
	(void)log_set_level(LOG_LEVEL_INFO);

	event_init();
	nvs_init();

	if (board_init() != STATUS_OK) {
		LOG_WARN("board_init failed (board=%s)", board_config_get()->name);
	} else {
		led_scene_run(LED_SCENE_ID_BOOTUP);
	}

	LOG_INFO("demo start ver=%s product=%s board=%s hw=%s", version_get_string(),
		 device_profile_product()->name, board_config_get()->name,
		 device_profile_hardware_name(device_profile_hardware_id()));
	LOG_INFO("mask=0x%x led=%u uart=%u", board_config_get()->mask, board_led_count(),
		 board_uart_count());
	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("demo run (led_scene update 50ms, heartbeat 1s)");

	while (1) {
		led_scene_update();
		if ((tick % 20U) == 0U) {
			LOG_INFO("heartbeat tick=%u board=%s", tick / 20U, board_config_get()->name);
		}
		tick++;
		k_sleep(K_MSEC(50));
	}
}
