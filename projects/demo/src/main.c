/*
 * projects/demo — thin Zephyr entry (kernel already running).
 */

#include "app.h"
#include "log.h"

int main(void)
{
	status_t st = app_startup();

	if (st != STATUS_OK)
	{
		LOG_ERROR("app_startup failed: %d", (int)st);
		return (int)st;
	}

	app_run();
	return 0;
}
