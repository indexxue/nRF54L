/**
 * @file app_event.h
 * @brief App wake bits on Zephyr k_event (no custom event layer).
 */

#ifndef COMMON_APP_EVENT_H
#define COMMON_APP_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include <zephyr/kernel.h>

/** Button flex-scan / wake — consumer clears after handling. */
#define APP_EVT_BUTTON   BIT(0)
/** BLE CMD parsed into business queue — ble_cmd_fetch(). */
#define APP_EVT_BLE_CMD  BIT(1)
/** GAP connect/disconnect — shallow-sleep wake. */
#define APP_EVT_BLE_GAP  BIT(2)

#define APP_EVT_ANY (APP_EVT_BUTTON | APP_EVT_BLE_CMD | APP_EVT_BLE_GAP)

/** Defined in start.c via K_EVENT_DEFINE(app_events). */
extern struct k_event app_events;

/**
 * Non-blocking test-and-clear for one or more bits.
 * @return true if any of @p bits was set (matched bits are cleared).
 */
static inline bool app_event_take(uint32_t bits)
{
	uint32_t got = k_event_wait(&app_events, bits, false, K_NO_WAIT) & bits;

	if (got == 0U) {
		return false;
	}
	k_event_clear(&app_events, got);
	return true;
}

#ifdef __cplusplus
}
#endif

#endif /* COMMON_APP_EVENT_H */
