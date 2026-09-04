/**
 * @file    led_scene.c
 * @brief   LED 场景状态机；GPIO 规格来自 board_config_t。
 */

#include "led_scene.h"

#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include "board.h"

static uint32_t s_last_update_ms;

typedef enum
{
    LED_IDX_0 = 0, /**< maps scene R channel */
    LED_IDX_1,     /**< maps scene B channel */
    LED_IDX_MAX
} led_idx_e;

typedef struct
{
    led_scene_id_e id;
    bool running;
    uint8_t current_cycle;
    uint8_t current_action;
    uint8_t action_cycle;
    uint32_t action_time;
    led_rgb_value_t current_rgb;
} led_scene_state_t;

typedef struct
{
    led_scene_state_t states[LED_SCENE_ID_MAX_NUM];
    led_scene_id_e active_scene;
    bool initialized;
    uint8_t led_count;
    const struct gpio_dt_spec *leds[LED_IDX_MAX];
} led_scene_self_t;

static led_scene_self_t self;

static const led_scene_t led_scene_bootup = {
    .cycle = CYCLE_ALWAYS,
    .num = 2,
    .action[0] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
    .action[1] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_pairing = {
    .cycle = 30,
    .num = 2,
    .action[0] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 1000 * LED_SCENE_MSEC,
    },
    .action[1] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 1000 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_trigger = {
    .cycle = 10,
    .num = 2,
    .action[0] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
    .action[1] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_error = {
    .cycle = 10,
    .num = 2,
    .action[0] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 100 * LED_SCENE_MSEC,
    },
    .action[1] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 100 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_success = {
    .cycle = 20,
    .num = 2,
    .action[0] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
    .action[1] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0xFF,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
};

static const led_scene_t led_scene_factory_run = {
    .cycle = 10,
    .num = 2,
    .action[0] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0xFF,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
    .action[1] = {
        .cycle = 1,
        .type = ACTION_ONOFF,
        .sub.onoff.value.r = 0,
        .sub.onoff.value.g = 0,
        .sub.onoff.value.b = 0,
        .sub.onoff.lifetime = 500 * LED_SCENE_MSEC,
    },
};

static const led_scene_tab_t scene_table[LED_SCENE_ID_MAX_NUM] = {
    [LED_SCENE_ID_BOOTUP] = {.prio = LED_SCENE_PRIO_LOW, .scene = &led_scene_bootup},
    [LED_SCENE_ID_PAIRING] = {.prio = LED_SCENE_PRIO_PAIR, .scene = &led_scene_pairing},
    [LED_SCENE_ID_TRIGGER] = {.prio = LED_SCENE_PRIO_NORMAL, .scene = &led_scene_trigger},
    [LED_SCENE_ID_ERROR] = {.prio = LED_SCENE_PRIO_NORMAL, .scene = &led_scene_error},
    [LED_SCENE_ID_SUCCESS] = {.prio = LED_SCENE_PRIO_NORMAL, .scene = &led_scene_success},
    [LED_SCENE_ID_FACTORY_RUN] = {.prio = LED_SCENE_PRIO_LOW, .scene = &led_scene_factory_run},
};

static void led_drv_onoff(led_idx_e idx, bool onoff)
{
    if ((uint8_t)idx >= self.led_count)
    {
        return;
    }
    if (self.leds[idx] == NULL)
    {
        return;
    }
    (void)gpio_pin_set_dt(self.leds[idx], onoff ? 1 : 0);
}

static void led_scene_output(const led_rgb_value_t *rgb)
{
    if (rgb == NULL)
    {
        led_drv_onoff(LED_IDX_0, false);
        led_drv_onoff(LED_IDX_1, false);
        return;
    }

    /* Scene tables use R/B channels mapped to led0 / led1 */
    led_drv_onoff(LED_IDX_0, rgb->r ? true : false);
    led_drv_onoff(LED_IDX_1, rgb->b ? true : false);
}

static led_scene_id_e led_scene_find_highest_priority(void)
{
    led_scene_id_e highest_id = LED_SCENE_ID_MAX_NUM;
    led_scene_prio_e highest_prio = LED_SCENE_PRIO_MAX_NUM;

    for (uint8_t i = 0; i < LED_SCENE_ID_MAX_NUM; i++)
    {
        if (self.states[i].running)
        {
            led_scene_prio_e prio = scene_table[i].prio;
            if (prio < highest_prio)
            {
                highest_prio = prio;
                highest_id = (led_scene_id_e)i;
            }
        }
    }

    return highest_id;
}

static void led_scene_sync_tick(void)
{
    s_last_update_ms = k_uptime_get_32();
}

void led_scene_update(void)
{
    if (!self.initialized)
    {
        return;
    }

    if (self.active_scene >= LED_SCENE_ID_MAX_NUM)
    {
        return;
    }

    led_scene_state_t *state = &self.states[self.active_scene];
    const led_scene_t *scene = scene_table[self.active_scene].scene;

    if (!state->running || scene == NULL)
    {
        return;
    }

    uint32_t now = k_uptime_get_32();
    uint32_t elapsed = now - s_last_update_ms;
    s_last_update_ms = now;
    if (elapsed == 0U)
    {
        return;
    }

    state->action_time += elapsed;

    const led_scene_action_t *action = &scene->action[state->current_action];
    bool action_complete = false;

    if (action->type == ACTION_ONOFF)
    {
        if (state->action_time >= action->sub.onoff.lifetime)
        {
            action_complete = true;
        }
        else
        {
            led_scene_output(&action->sub.onoff.value);
        }
    }
    else if (action->type == ACTION_FADE)
    {
        if (state->action_time >= action->sub.fade.interval)
        {
            bool fade_complete = false;

            if (action->sub.fade.start_value.r < action->sub.fade.end_value.r)
            {
                if (state->current_rgb.r + action->sub.fade.step >= action->sub.fade.end_value.r)
                {
                    state->current_rgb.r = action->sub.fade.end_value.r;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.r =
                        (uint8_t)(state->current_rgb.r + action->sub.fade.step);
                }
            }
            else if (action->sub.fade.start_value.r > action->sub.fade.end_value.r)
            {
                if (state->current_rgb.r <= action->sub.fade.step ||
                    state->current_rgb.r - action->sub.fade.step <= action->sub.fade.end_value.r)
                {
                    state->current_rgb.r = action->sub.fade.end_value.r;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.r =
                        (uint8_t)(state->current_rgb.r - action->sub.fade.step);
                }
            }

            if (action->sub.fade.start_value.b < action->sub.fade.end_value.b)
            {
                if (state->current_rgb.b + action->sub.fade.step >= action->sub.fade.end_value.b)
                {
                    state->current_rgb.b = action->sub.fade.end_value.b;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.b =
                        (uint8_t)(state->current_rgb.b + action->sub.fade.step);
                }
            }
            else if (action->sub.fade.start_value.b > action->sub.fade.end_value.b)
            {
                if (state->current_rgb.b <= action->sub.fade.step ||
                    state->current_rgb.b - action->sub.fade.step <= action->sub.fade.end_value.b)
                {
                    state->current_rgb.b = action->sub.fade.end_value.b;
                    fade_complete = true;
                }
                else
                {
                    state->current_rgb.b =
                        (uint8_t)(state->current_rgb.b - action->sub.fade.step);
                }
            }

            led_scene_output(&state->current_rgb);
            state->action_time = 0;

            if (fade_complete)
            {
                action_complete = true;
            }
        }
    }

    if (action_complete)
    {
        state->action_cycle++;
        if (state->action_cycle >= action->cycle)
        {
            state->action_cycle = 0;
            state->current_action++;
            if (state->current_action >= scene->num)
            {
                state->current_action = 0;
                state->current_cycle++;
                if (scene->cycle != CYCLE_ALWAYS && state->current_cycle >= scene->cycle)
                {
                    state->running = false;
                    led_scene_output(NULL);
                    self.active_scene = led_scene_find_highest_priority();
                    led_scene_sync_tick();
                    if (self.active_scene >= LED_SCENE_ID_MAX_NUM)
                    {
                        return;
                    }
                    state = &self.states[self.active_scene];
                    scene = scene_table[self.active_scene].scene;
                }
            }
        }
        state->action_time = 0;
        if (action->type == ACTION_FADE)
        {
            state->current_rgb = action->sub.fade.start_value;
        }
    }
}

status_t led_scene_init(void)
{
    memset(&self, 0, sizeof(led_scene_self_t));
    self.active_scene = LED_SCENE_ID_MAX_NUM;

    if (!board_wants(BOARD_MASK_LED))
    {
        return STATUS_NOT_SUPPORTED;
    }

    const uint8_t n = board_led_count();
    if (n == 0U)
    {
        return STATUS_FAIL;
    }

    self.led_count = (n > (uint8_t)LED_IDX_MAX) ? (uint8_t)LED_IDX_MAX : n;

    for (uint8_t i = 0; i < self.led_count; i++)
    {
        const board_led_spec_t *spec = board_led_spec(i);
        if ((spec == NULL) || (spec->gpio == NULL) || !gpio_is_ready_dt(spec->gpio))
        {
            return STATUS_FAIL;
        }
        if (gpio_pin_configure_dt(spec->gpio, GPIO_OUTPUT_INACTIVE) != 0)
        {
            return STATUS_FAIL;
        }
        self.leds[i] = spec->gpio;
        led_drv_onoff((led_idx_e)i, false);
    }

    led_scene_sync_tick();
    self.initialized = true;

    return STATUS_OK;
}

void led_scene_led_direct_set(led_scene_led_e led, bool on)
{
    if (!self.initialized)
    {
        return;
    }
    if ((uint8_t)led < self.led_count)
    {
        led_drv_onoff((led_idx_e)led, on);
    }
}

void led_scene_run(led_scene_id_e id)
{
    if (id >= LED_SCENE_ID_MAX_NUM)
    {
        return;
    }

    if (!self.initialized)
    {
        return;
    }

    led_scene_state_t *state = &self.states[id];
    const led_scene_t *scene = scene_table[id].scene;

    if (scene == NULL)
    {
        return;
    }

    if (state->running)
    {
        return;
    }

    state->running = true;
    state->current_cycle = 0;
    state->current_action = 0;
    state->action_cycle = 0;
    state->action_time = 0;
    state->current_rgb.r = 0;
    state->current_rgb.g = 0;
    state->current_rgb.b = 0;

    led_scene_sync_tick();

    led_scene_id_e new_scene = led_scene_find_highest_priority();
    if (new_scene != self.active_scene)
    {
        if (self.active_scene < LED_SCENE_ID_MAX_NUM)
        {
            self.states[self.active_scene].running = false;
        }
        self.active_scene = new_scene;
    }

    if (self.active_scene < LED_SCENE_ID_MAX_NUM)
    {
        led_scene_state_t *active_state = &self.states[self.active_scene];
        active_state->current_cycle = 0;
        active_state->current_action = 0;
        active_state->action_cycle = 0;
        active_state->action_time = 0;
        active_state->current_rgb.r = 0;
        active_state->current_rgb.g = 0;
        active_state->current_rgb.b = 0;
        const led_scene_action_t *action = &scene_table[self.active_scene].scene->action[0];
        if (action->type == ACTION_FADE)
        {
            active_state->current_rgb = action->sub.fade.start_value;
        }
    }
}

void led_scene_cancel(led_scene_id_e id)
{
    if (id >= LED_SCENE_ID_MAX_NUM)
    {
        return;
    }

    if (!self.initialized)
    {
        return;
    }

    led_scene_state_t *state = &self.states[id];
    state->running = false;

    if (self.active_scene == id)
    {
        led_scene_output(NULL);
        led_scene_id_e new_scene = led_scene_find_highest_priority();
        if (new_scene < LED_SCENE_ID_MAX_NUM)
        {
            self.active_scene = new_scene;
            led_scene_sync_tick();
            led_scene_state_t *active_state = &self.states[self.active_scene];
            active_state->current_cycle = 0;
            active_state->current_action = 0;
            active_state->action_cycle = 0;
            active_state->action_time = 0;
            active_state->current_rgb.r = 0;
            active_state->current_rgb.g = 0;
            active_state->current_rgb.b = 0;
            const led_scene_action_t *action = &scene_table[self.active_scene].scene->action[0];
            if (action->type == ACTION_FADE)
            {
                active_state->current_rgb = action->sub.fade.start_value;
            }
        }
        else
        {
            self.active_scene = LED_SCENE_ID_MAX_NUM;
        }
    }
}
