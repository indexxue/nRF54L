/**
 * @file zigbee_app.h
 * @brief Zigbee ED：起栈 / 搜网 / leave；状态入队供 app 消费。
 */

#ifndef ZIGBEE_DEMO_ZIGBEE_APP_H
#define ZIGBEE_DEMO_ZIGBEE_APP_H

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ZIGBEE_APP_STATE_IDLE = 0,
	ZIGBEE_APP_STATE_STEERING,
	ZIGBEE_APP_STATE_JOINED,
	ZIGBEE_APP_STATE_FAILED,
} zigbee_app_state_t;

typedef struct {
	zigbee_app_state_t state;
	uint16_t nwk_addr;
	uint8_t ieee[8];
	bool ieee_valid;
} zigbee_app_evt_t;

status_t zigbee_app_start(void);
void zigbee_app_steer_request(void);
void zigbee_app_leave_request(void);

/** 非阻塞取事件；无事件返回 false。 */
bool zigbee_app_evt_fetch(zigbee_app_evt_t *out);

#ifdef __cplusplus
}
#endif

#endif /* ZIGBEE_DEMO_ZIGBEE_APP_H */
