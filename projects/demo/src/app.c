/*
 * demo — app_startup / app_run (Zephyr main thread).
 * No serial cmd framework here (cmd is factory-private under lib/cmd/).
 */

#include "app.h"

#include <zephyr/kernel.h>

#include "device_profile.h"
#include "event.h"
#include "fw_version.h"
#include "log.h"
#include "nvs.h"

status_t app_startup(void)
{
	if (log_init(NULL) != STATUS_OK)
	{
		return STATUS_FAIL;
	}
	(void)log_set_level(LOG_LEVEL_INFO);

	event_init();
	nvs_init();

	/* Private drivers: call lib/<module> init here when added. */

	/* Extra threads: K_THREAD_DEFINE / k_thread_create when needed. */

	LOG_INFO("demo app_startup ok ver=%s product=%s hw=%s", version_get_string(),
		 device_profile_product()->name,
		 device_profile_hardware_name(device_profile_hardware_id()));
	return STATUS_OK;
}

void app_run(void)
{
	LOG_INFO("demo app_run");

	while (1)
	{
		k_sleep(K_SECONDS(10));
		LOG_INFO("heartbeat");
	}
}
