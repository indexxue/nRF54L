/**
 * @file input.h
 * @brief 轻量板级中断输入调度：表来自 board_irq_spec；ISR → event → schedule。
 */

#ifndef COMMON_INPUT_H
#define COMMON_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "type.h"

typedef enum
{
	INPUT_STATE_RELEASED = 0, /**< 未处于 active 电平 */
	INPUT_STATE_ASSERTED = 1, /**< 处于 active 电平（有效） */
} input_state_e;

/**
 * @param id    board_irq_spec.id
 * @param name  board_irq_spec.name（可为 NULL）
 * @param state 去抖后的稳定状态
 */
typedef void (*input_notify_t)(uint8_t id, const char *name, input_state_e state);

/**
 * @brief 从 board_irq_* 装载通道，配置双边沿 GPIO 中断。
 * @return STATUS_OK / STATUS_NOT_SUPPORTED（无 IRQ mask）/ STATUS_FAIL
 */
status_t input_init(input_notify_t notify);
void input_deinit(void);

/** 主循环调用：若 EVT_ID_INPUT 置位则去抖并回调 */
void input_schedule(void);

void input_disable_all(void);
void input_enable_all(void);

void input_log_notify(uint8_t id, const char *name, input_state_e state);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_INPUT_H */
