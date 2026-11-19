/*
 * Shared platform bring-up for all projects under projects/.
 * Each app implements app_init() + app_run(); main lives in start/src/main.c.
 */

#ifndef START_H
#define START_H

#ifdef __cplusplus
extern "C" {
#endif

#include "type.h"

/**
 * Platform boot: log → nvs → board → (optional HFXO) → banner →
 * mask-gated LED bootup / button. App wake bits: K_EVENT_DEFINE(app_events).
 * Call once from main before app_init().
 */
status_t start_boot(void);

/**
 * Common per-tick drivers: led_scene / button / FTM seq.
 * Call from app_run() each loop iteration.
 */
void start_poll(void);

/** App must implement: project-specific init after start_boot(). */
status_t app_init(void);

/** App must implement: main-thread loop (normally never returns). */
void app_run(void);

#ifdef __cplusplus
}
#endif

#endif /* START_H */
