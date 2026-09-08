/**
 * @file    led_scene.h
 * @brief   LED 场景状态机（API 对齐 STM32 `tmp/led_scene`）；GPIO 来自 board_config。
 */

#ifndef COMMON_LED_SCENE_H
#define COMMON_LED_SCENE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "type.h"

#define LED_SCENE_ACTION_NUM 3
#define LED_SCENE_MSEC       1
#define CYCLE_ALWAYS         0xFFFF
/** 场景通道上限（实际颗数取 board_led_count） */
#define LED_SCENE_LED_NUM    2

typedef enum
{
    LED_SCENE_LED_0 = 0,
    LED_SCENE_LED_1,
    LED_SCENE_LED_MAX_NUM,
} led_scene_led_e;

typedef enum
{
    LED_SCENE_PRIO_FACTORY = 0,
    LED_SCENE_PRIO_PAIR,
    LED_SCENE_PRIO_NORMAL,
    LED_SCENE_PRIO_LOW,
    LED_SCENE_PRIO_MAX_NUM,
} led_scene_prio_e;

typedef enum
{
    LED_SCENE_ID_BOOTUP = 0,
    LED_SCENE_ID_PAIRING,
    LED_SCENE_ID_TRIGGER,
    LED_SCENE_ID_ERROR,
    LED_SCENE_ID_SUCCESS,
    LED_SCENE_ID_FACTORY_RUN,
    LED_SCENE_ID_EPD_2IN9, /**< 绿灯常亮：当前 2.9" 驱动 */
    LED_SCENE_ID_EPD_4IN2, /**< 蓝灯常亮：当前 4.2" 驱动 */
    LED_SCENE_ID_MAX_NUM,
} led_scene_id_e;

typedef struct
{
    uint8_t r;
    uint8_t g;
    uint8_t b;
} led_rgb_value_t;

typedef enum
{
    ACTION_ONOFF = 0,
    ACTION_FADE,
} led_action_type_e;

typedef struct
{
    uint8_t cycle;
    led_action_type_e type;
    union
    {
        struct
        {
            led_rgb_value_t value;
            uint32_t lifetime;
        } onoff;
        struct
        {
            led_rgb_value_t start_value;
            led_rgb_value_t end_value;
            uint8_t step;
            uint32_t interval;
        } fade;
    } sub;
} led_scene_action_t;

typedef struct
{
    uint16_t cycle;
    uint8_t num;
    led_scene_action_t action[LED_SCENE_ACTION_NUM];
} led_scene_t;

typedef struct
{
    led_scene_prio_e prio;
    const led_scene_t *scene;
} led_scene_tab_t;

/**
 * @brief 从 `board_config_get()` 读取 LED 规格并配置输出。
 * @return STATUS_OK / STATUS_NOT_SUPPORTED（无 LED mask）/ STATUS_FAIL
 */
status_t led_scene_init(void);

/** 由应用周期调用（建议 10–50 ms）；内部按 `k_uptime_get_32` 累计时长 */
void led_scene_update(void);
void led_scene_run(led_scene_id_e id);
void led_scene_cancel(led_scene_id_e id);
void led_scene_led_direct_set(led_scene_led_e led, bool on);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_LED_SCENE_H */
