/*
 * demo — project logic only (platform boot is in start/).
 */

#include "start.h"

#include <zephyr/kernel.h>

#include "board.h"
#include "device_profile.h"
#include "log.h"

status_t app_init(void)
{
	LOG_INFO("demo app_init ok hw=%s",
		 device_profile_hardware_name(device_profile_hardware_id()));
	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("demo run (start_poll 50ms, heartbeat 1s)");

	while (1) {
		start_poll();
		if ((tick % 20U) == 0U) {
			LOG_INFO("heartbeat tick=%u board=%s", tick / 20U, board_config_get()->name);
		}
		tick++;
		k_sleep(K_MSEC(50));
	}
}
