/**
 * @file input.c
 * @brief 板级 IRQ GPIO：Zephyr 中断回调 + event + 去抖 schedule（轻量）。
 */

#include "input.h"

#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "board.h"
#include "event.h"
#include "log.h"

#define INPUT_MAX_NUM     8U
#define INPUT_DEBOUNCE_MS 20U

typedef struct
{
	uint8_t id;
	const char *name;
	const struct gpio_dt_spec *gpio;
	uint8_t active_level; /**< 保留；读脚用 gpio_pin_get_dt（逻辑 1=asserted） */
	input_state_e old_state;
	volatile uint8_t triggered;
	struct gpio_callback cb;
	bool enabled;
} input_io_t;

typedef struct
{
	uint8_t num;
	input_io_t *list;
	input_notify_t notify;
	bool ready;
} input_self_t;

static input_io_t s_input_list[INPUT_MAX_NUM];
static input_self_t self;

static input_state_e input_read_state(const input_io_t *in)
{
	int val;

	if ((in == NULL) || (in->gpio == NULL))
	{
		return INPUT_STATE_RELEASED;
	}

	val = gpio_pin_get_dt(in->gpio);
	if (val < 0)
	{
		return INPUT_STATE_RELEASED;
	}
	/* gpio_pin_get_dt：1 = 逻辑 active（与 DT ACTIVE_* 一致） */
	return (val != 0) ? INPUT_STATE_ASSERTED : INPUT_STATE_RELEASED;
}

static void input_gpio_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	input_io_t *in = CONTAINER_OF(cb, input_io_t, cb);

	(void)port;
	(void)pins;

	in->triggered = 1U;
	event_set_from_isr(EVT_ID_INPUT);
}

static status_t input_load_from_board(void)
{
	uint8_t n = board_irq_count();

	if (n == 0U)
	{
		return STATUS_NOT_SUPPORTED;
	}
	if (n > INPUT_MAX_NUM)
	{
		n = INPUT_MAX_NUM;
	}

	self.num = 0U;
	for (uint8_t i = 0; i < n; i++)
	{
		const board_irq_spec_t *spec = board_irq_spec(i);
		input_io_t *in = &s_input_list[self.num];
		gpio_flags_t flags;

		if ((spec == NULL) || (spec->gpio == NULL))
		{
			continue;
		}
		if (!gpio_is_ready_dt(spec->gpio))
		{
			LOG_WARN("input: gpio not ready id=%u name=%s", spec->id,
				 spec->name != NULL ? spec->name : "?");
			continue;
		}

		memset(in, 0, sizeof(*in));
		in->id = spec->id;
		in->name = spec->name;
		in->gpio = spec->gpio;
		in->active_level = spec->active_level;
		in->enabled = true;

		if (gpio_pin_configure_dt(spec->gpio, GPIO_INPUT) != 0)
		{
			LOG_WARN("input: configure fail id=%u", spec->id);
			continue;
		}

		flags = GPIO_INT_EDGE_BOTH;
		if (gpio_pin_interrupt_configure_dt(spec->gpio, flags) != 0)
		{
			LOG_WARN("input: irq configure fail id=%u", spec->id);
			continue;
		}

		gpio_init_callback(&in->cb, input_gpio_isr, BIT(spec->gpio->pin));
		if (gpio_add_callback(spec->gpio->port, &in->cb) != 0)
		{
			LOG_WARN("input: add callback fail id=%u", spec->id);
			continue;
		}

		in->old_state = input_read_state(in);
		in->triggered = 0U;
		self.num++;
	}

	self.list = s_input_list;
	return (self.num > 0U) ? STATUS_OK : STATUS_FAIL;
}

status_t input_init(input_notify_t notify)
{
	memset(&self, 0, sizeof(self));
	self.notify = notify;

	if (!board_wants(BOARD_MASK_IRQ))
	{
		return STATUS_NOT_SUPPORTED;
	}

	if (input_load_from_board() != STATUS_OK)
	{
		return STATUS_FAIL;
	}

	self.ready = true;
	LOG_INFO("input: init ok channels=%u", self.num);
	return STATUS_OK;
}

void input_deinit(void)
{
	if (self.ready && (self.list != NULL))
	{
		for (uint8_t i = 0; i < self.num; i++)
		{
			input_io_t *in = &self.list[i];

			if (in->gpio != NULL)
			{
				(void)gpio_pin_interrupt_configure_dt(in->gpio, GPIO_INT_DISABLE);
				(void)gpio_remove_callback(in->gpio->port, &in->cb);
			}
		}
	}
	memset(&self, 0, sizeof(self));
}

void input_schedule(void)
{
	if (!self.ready || (self.list == NULL) || (self.num == 0U))
	{
		return;
	}

	if (!event_is_set(EVT_ID_INPUT))
	{
		return;
	}

	for (uint8_t i = 0; i < self.num; i++)
	{
		input_io_t *in = &self.list[i];
		input_state_e s1;
		input_state_e s2;
		input_state_e new_state;

		if (!in->enabled || (in->triggered == 0U))
		{
			continue;
		}

		in->triggered = 0U;

		s1 = input_read_state(in);
		k_msleep(INPUT_DEBOUNCE_MS);
		s2 = input_read_state(in);
		if (s1 != s2)
		{
			continue;
		}

		new_state = s1;
		if (new_state == in->old_state)
		{
			continue;
		}

		in->old_state = new_state;

		if (self.notify != NULL)
		{
			self.notify(in->id, in->name, new_state);
		}
	}
}

void input_disable_all(void)
{
	if (!self.ready || (self.list == NULL))
	{
		return;
	}

	for (uint8_t i = 0; i < self.num; i++)
	{
		input_io_t *in = &self.list[i];

		if (in->gpio != NULL)
		{
			(void)gpio_pin_interrupt_configure_dt(in->gpio, GPIO_INT_DISABLE);
		}
		in->enabled = false;
		in->triggered = 0U;
	}
}

void input_enable_all(void)
{
	if (!self.ready || (self.list == NULL))
	{
		return;
	}

	for (uint8_t i = 0; i < self.num; i++)
	{
		input_io_t *in = &self.list[i];

		if (in->gpio == NULL)
		{
			continue;
		}
		in->old_state = input_read_state(in);
		in->triggered = 0U;
		(void)gpio_pin_interrupt_configure_dt(in->gpio, GPIO_INT_EDGE_BOTH);
		in->enabled = true;
	}
}

void input_log_notify(uint8_t id, const char *name, input_state_e state)
{
	LOG_INFO("input id=%u name=%s state=%s", id, name != NULL ? name : "?",
		 state == INPUT_STATE_ASSERTED ? "ASSERT" : "RELEASE");
}
