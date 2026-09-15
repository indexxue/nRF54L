/**
 * @file button.h
 * @brief 业务按键层：包 flexible_button；表来自 board_button_spec。
 */

#ifndef COMMON_BUTTON_H
#define COMMON_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

typedef enum
{
	BTN_STATUS_UP = 0,
	BTN_STATUS_DOWN,
} btn_status_e;

typedef enum
{
	BTN_ID_UP = 0,
	BTN_ID_DOWN,
	BTN_ID_LEFT,
	BTN_ID_RIGHT,
	BTN_ID_CONFIRM,
	BTN_ID_BACK,
	BTN_ID_MAX_NUMBER,
} btn_id_e;

typedef enum
{
	BTN_EVENT_NONE = 0,
	BTN_EVENT_PRESS_DOWN,
	BTN_EVENT_PRESS_UP,
	BTN_EVENT_SINGLE_CLICK,
	BTN_EVENT_DOUBLE_CLICK,
	BTN_EVENT_REPEAT_CLICK,
	BTN_EVENT_LONG_PRESS,
	BTN_EVENT_LONG_HOLD,
	BTN_EVENT_LONG_HOLD_UP,
} btn_event_e;

typedef enum
{
	BTN_PERMISSION_NONE = 0,
	BTN_PERMISSION_RESET = (1 << 0),
	BTN_PERMISSION_PAIR = (1 << 1),
	BTN_PERMISSION_FTM = (1 << 2),
	BTN_PERMISSION_BYPASS = (1 << 3),
	BTN_PERMISSION_UNPAIR = (1 << 4),
	BTN_PERMISSION_ALARM = (1 << 5),
	BTN_PERMISSION_UP = (1 << 6),
	BTN_PERMISSION_DOWN = (1 << 7),
	BTN_PERMISSION_LEFT = (1 << 8),
	BTN_PERMISSION_RIGHT = (1 << 9),
	BTN_PERMISSION_CONFIRM = (1 << 10),
	BTN_PERMISSION_BACK = (1 << 11),
	BTN_PERMISSION_WAKE = (1 << 12), /**< 可从浅睡 GPIO 唤醒 */
} btn_permission_e;

/** 按键事件回调（须配合周期性 button_schedule）。 */
typedef void (*btn_notify_t)(btn_id_e id, const char *name, btn_permission_e permission,
			     btn_event_e event);

/**
 * @brief 从 board_button_spec 建表并配置 GPIO。
 * @return STATUS_OK；无 BUTTON mask / 0 键时 STATUS_NOT_SUPPORTED（非失败）。
 */
status_t button_init(btn_notify_t notify);
void button_deinit(void);

/** 按 FLEX_BTN_SCAN_FREQ_HZ（默认 50Hz）周期调用，例如每 20ms。 */
void button_schedule(void);

/**
 * FTM 跳槽：带 BTN_PERMISSION_FTM 的键，连击 5→4→3→2 后切换 boot_mode 并冷复位。
 * factory→app（不擦 slot1），其它→factory。错步或步间超时从 5 重来。
 * 非 FTM 键不消费事件。由 start_poll() 调用。
 */
void button_ftm_seq_poll(void);

/**
 * 仅为带 BTN_PERMISSION_WAKE 的键开边沿 IRQ；ISR 只 event_set_from_isr(EVT_ID_BUTTON)。
 * 浅睡前 enable，醒来后 disable（与 button_schedule 互斥使用）。
 */
void button_wake_enable(void);
void button_wake_disable(void);

void button_last_event_get(btn_id_e *id, btn_event_e *event);
/** 最近一次连击次数（1=单击 … 5=连击五次）；无事件时为 0。 */
uint8_t button_last_click_count_get(void);
uint16_t button_last_permission_get(void);
void button_last_event_clear(void);

const char *button_id_to_str(btn_id_e id);
const char *button_event_to_str(btn_event_e event);

/** 默认日志回调：单击/双击/长按等打 LOG_INFO。 */
void button_log_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event);

/** 逻辑电平 0/1；未找到返回 0xFF。 */
uint8_t button_get_level(btn_id_e id);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_BUTTON_H */
