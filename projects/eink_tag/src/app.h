/*
 * Application lifecycle (Zephyr RTOS). Entry is main(); this file owns init order + run loop.
 */

#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

status_t app_startup(void);
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */
