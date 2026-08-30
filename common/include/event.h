/**
 * @file event.h
 * @brief Bitmask event flags (API from STM32 Common; Zephyr sync).
 */

#ifndef COMMON_EVENT_H
#define COMMON_EVENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef enum
{
    EVT_ID_NONE = 0x00000000,
    EVT_ID_BUTTON = 0x00000001,
    EVT_ID_INPUT = 0x00000002,
    EVT_ID_TIMER = 0x00000004,
    EVT_ID_WATCHDOG = 0x00000008,
} event_id_e;

void event_init(void);
void event_set(event_id_e id);
void event_set_from_isr(event_id_e id);
bool event_is_set(event_id_e id);
void event_clear(event_id_e id);
void event_schedule(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_EVENT_H */
