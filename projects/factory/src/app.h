/*
 * Application lifecycle (Zephyr RTOS). Entry is main(); this file owns init order + run loop.
 */

#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/**
 * Synchronous bring-up: log → event → (lib hw) → cmd → threads.
 * Return STATUS_OK to enter app_run(); otherwise main logs and exits.
 */
status_t app_startup(void);

/**
 * Main-thread body after startup. Does not return under normal operation.
 */
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
