/*
 * Shared Zephyr entry for all projects. Kernel is already running.
 */

#include "start.h"

#include "log.h"

int main(void)
{
	status_t st = start_boot();

	if (st != STATUS_OK) {
		LOG_ERROR("start_boot failed: %d", (int)st);
		return (int)st;
	}

	st = app_init();
	if (st != STATUS_OK) {
		LOG_ERROR("app_init failed: %d", (int)st);
		return (int)st;
	}

	app_run();
	return 0;
}
