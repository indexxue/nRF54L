/**
 * @file fw_version.h
 * @brief Firmware version string (shared; set via CONFIG_APP_VERSION_STRING).
 *
 * Named fw_version.h to avoid clashing with Zephyr's generated version.h.
 */

#ifndef COMMON_FW_VERSION_H
#define COMMON_FW_VERSION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

#include "type.h"

#define VERSION_STRING_MAX (32U)

/**
 * Return static NUL-terminated semver string (e.g. "0.0.1").
 * From Kconfig CONFIG_APP_VERSION_STRING; default "0.0.0".
 */
const char *version_get_string(void);

/**
 * Copy version into buf (always NUL-terminated when cap > 0).
 * @return STATUS_OK, or STATUS_INVALID_ARG if buf is NULL / cap is 0.
 */
status_t version_copy(char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_FW_VERSION_H */
