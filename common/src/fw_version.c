/**
 * @file fw_version.c
 * @brief Firmware version string from Kconfig CONFIG_APP_VERSION_STRING.
 */

#include "fw_version.h"

#include <string.h>

#include <zephyr/kernel.h>

const char *version_get_string(void)
{
#ifdef CONFIG_APP_VERSION_STRING
	return CONFIG_APP_VERSION_STRING;
#else
	return "0.0.0";
#endif
}

status_t version_copy(char *buf, size_t cap)
{
	const char *ver;
	size_t len;

	if ((buf == NULL) || (cap == 0U))
	{
		return STATUS_INVALID_ARG;
	}

	ver = version_get_string();
	len = strlen(ver);
	if (len >= cap)
	{
		len = cap - 1U;
	}

	(void)memcpy(buf, ver, len);
	buf[len] = '\0';
	return STATUS_OK;
}
