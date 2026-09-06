/**
 * @file ble_cmd_thread.c
 * @brief CMD 线程：由主进程 ble_init() → ble_cmd_thread_start() 动态创建。
 */

#include "ble_cmd.h"
#include "ble_cmd_internal.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "event.h"
#include "log.h"

#define BLE_CMD_THREAD_STACK 2048
#define BLE_CMD_THREAD_PRIO  5

static K_THREAD_STACK_DEFINE(s_cmd_stack, BLE_CMD_THREAD_STACK);
static struct k_thread s_cmd_thread;
static k_tid_t s_cmd_tid;
static atomic_t s_started;

static void ble_cmd_thread_fn(void *p1, void *p2, void *p3)
{
	ble_cmd_raw_t raw;
	ble_cmd_msg_t msg;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INFO("ble_cmd: thread running");

	for (;;) {
		if (!ble_cmd_raw_get(&raw, -1)) {
			continue;
		}

		if (ble_cmd_parse(raw.data, raw.len, &msg) != STATUS_OK) {
			LOG_WARN("ble_cmd: drop bad frame len=%u", (unsigned int)raw.len);
			continue;
		}

		LOG_INFO("ble_cmd: parsed id=%u plen=%u", (unsigned int)msg.cmd_id,
			 (unsigned int)msg.payload_len);

		if (ble_cmd_parsed_put(&msg) != STATUS_OK) {
			continue;
		}

		event_set(EVT_ID_BLE_CMD);
	}
}

status_t ble_cmd_thread_start(void)
{
	if (atomic_cas(&s_started, 0, 1) == 0) {
		LOG_INFO("ble_cmd: thread already started");
		return STATUS_OK;
	}

	s_cmd_tid = k_thread_create(&s_cmd_thread, s_cmd_stack,
				    K_THREAD_STACK_SIZEOF(s_cmd_stack), ble_cmd_thread_fn, NULL,
				    NULL, NULL, BLE_CMD_THREAD_PRIO, 0, K_NO_WAIT);
	if (s_cmd_tid == NULL) {
		atomic_set(&s_started, 0);
		LOG_ERROR("ble_cmd: k_thread_create failed");
		return STATUS_FAIL;
	}

	k_thread_name_set(s_cmd_tid, "ble_cmd");
	LOG_INFO("ble_cmd: thread created by main (ble_init)");
	return STATUS_OK;
}
