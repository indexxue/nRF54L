/**
 * @file fw_version.c
 * @brief Firmware version string from Kconfig CONFIG_APP_VERSION_STRING.
 */

#include "fw_version.h"

const char *fw_version_get_string(void)
{
#ifdef CONFIG_APP_VERSION_STRING
	return CONFIG_APP_VERSION_STRING;
#else
	return "0.0.0";
#endif
}
