/*
 * factory — project logic only (platform boot is in start/).
 */

#include "start.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cmd.h"
#include "device_profile.h"
#include "log.h"

status_t app_init(void)
{
	cmd_init(NULL, NULL);
	cmd_register_defaults();

	LOG_INFO("factory app_init ok hw=%s",
		 device_profile_hardware_name(device_profile_hardware_id()));
	printk("factory printk ok (uart20 P1.09/P1.10)\n");
	return STATUS_OK;
}

void app_run(void)
{
	LOG_INFO("factory app_run (type help)");

	while (1) {
		start_poll();
		k_sleep(K_SECONDS(10));
		LOG_INFO("factory heartbeat");
	}
}
