/**
 * @file ble_stack.c
 * @brief bt_enable 与协议栈就绪状态。
 */

#include "ble_stack.h"

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>

#include "ble_evt.h"
#include "log.h"

static atomic_t s_ready;

status_t ble_stack_init(void)
{
	int err;

	err = bt_enable(NULL);
	if (err != 0) {
		LOG_ERROR("ble_stack: bt_enable %d", err);
		return STATUS_FAIL;
	}

	atomic_set(&s_ready, 1);
	ble_evt_post(BLE_EVT_STACK_READY, 0U);
	LOG_INFO("ble_stack: ready (SoftDevice Host)");
	return STATUS_OK;
}

bool ble_stack_is_ready(void)
{
	return atomic_get(&s_ready) != 0;
}
