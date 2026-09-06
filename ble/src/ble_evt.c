/**
 * @file ble_evt.c
 * @brief 协议层可选回调分发。
 */

#include "ble_evt.h"

#include <stddef.h>

static ble_evt_cb_t s_cb;
static void *s_user;

void ble_evt_set_callback(ble_evt_cb_t cb, void *user)
{
	s_cb = cb;
	s_user = user;
}

void ble_evt_post(ble_evt_id_t id, uint8_t reason)
{
	ble_evt_t evt = {
		.id = id,
		.reason = reason,
	};

	if (s_cb != NULL) {
		s_cb(&evt, s_user);
	}
}
