/**
 * @file ble.c
 * @brief BLE 协议层组装（由主进程 app_init → ble_init 调用）。
 *
 * 顺序：CMD 线程 → stack → gatt → gap 广播。
 */

#include "ble.h"

#include "ble_cmd.h"
#include "ble_gap.h"
#include "ble_gatt.h"
#include "ble_stack.h"
#include "board.h"
#include "log.h"

status_t ble_init(void)
{
	status_t st;

	if (!board_wants(BOARD_MASK_BLE)) {
		return STATUS_NOT_SUPPORTED;
	}

	/* 主进程内创建并启动 CMD 处理线程（须先于 GATT Write / inject） */
	st = ble_cmd_thread_start();
	if (st != STATUS_OK) {
		return st;
	}

	st = ble_stack_init();
	if (st != STATUS_OK) {
		return st;
	}

	st = ble_gatt_init();
	if (st != STATUS_OK) {
		return st;
	}

	st = ble_gap_start_adv();
	if (st != STATUS_OK) {
		return st;
	}

	LOG_INFO("ble: init ok (CMD 4-digit zones; PING/DISPLAY/ACK)");
	return STATUS_OK;
}

status_t ble_stop(void)
{
	status_t st;

	if (!board_wants(BOARD_MASK_BLE)) {
		return STATUS_NOT_SUPPORTED;
	}

	st = ble_gap_stop_adv();
	if (st != STATUS_OK) {
		return st;
	}

	LOG_INFO("ble: stopped (adv off; CMD thread kept)");
	return STATUS_OK;
}

bool ble_is_ready(void)
{
	return ble_stack_is_ready();
}

bool ble_is_advertising(void)
{
	return ble_gap_is_advertising();
}

bool ble_is_connected(void)
{
	return ble_gap_is_connected();
}

status_t ble_adv_start(void)
{
	return ble_gap_start_adv();
}

status_t ble_adv_stop(void)
{
	return ble_gap_stop_adv();
}

void ble_adv_set_restart_on_disconnect(bool enable)
{
	ble_gap_set_restart_on_disconnect(enable);
}

status_t ble_adv_set_slow(bool slow)
{
	return ble_gap_set_adv_slow(slow);
}

bool ble_adv_is_slow(void)
{
	return ble_gap_is_adv_slow();
}

uint32_t ble_count_connected(void)
{
	return ble_gap_count_connected();
}

uint32_t ble_count_disconnected(void)
{
	return ble_gap_count_disconnected();
}
