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
    EVT_ID_BLE_CMD = 0x00000010, /**< BLE 标定 CMD 已入业务队列，ble_cmd_fetch() */
    EVT_ID_BLE_GAP = 0x00000020, /**< GAP 连接等，用于浅睡唤醒 */
} event_id_e;

void event_init(void);
void event_set(event_id_e id);
void event_set_from_isr(event_id_e id);
bool event_is_set(event_id_e id);
void event_clear(event_id_e id);
void event_schedule(void);

/** 丢掉挂起的 wait token（进睡前调用，避免立刻被旧事件唤醒）。 */
void event_wait_reset(void);

/**
 * 等待任意 event_set / event_set_from_isr。
 * @param timeout_ms <0 永久；0 只取已有 token；>0 超时毫秒
 * @return true=被事件唤醒，false=超时或未就绪
 */
bool event_wait(int32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_EVENT_H */
