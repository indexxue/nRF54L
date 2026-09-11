/**
 * @file button.c
 * @brief 业务按键：board_button_spec + flexible_button（Zephyr GPIO）。
 */

#include "button.h"

#include <string.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/util.h>

#include "board.h"
#include "event.h"
#include "flexible_button.h"
#include "log.h"

#define BUTTON_MAX_NUM 8U

typedef struct
{
	btn_id_e id;
	const char *name;
	const struct gpio_dt_spec *gpio;
	uint8_t active_level;
	uint16_t permission;
	flex_button_t flex;
} button_list_t;

typedef struct
{
	uint8_t num;
	button_list_t *list;
	btn_notify_t notify;
	bool ready;
	bool wake_irq_on;
	bool wake_cb_ready;
	struct gpio_callback wake_cb;
} button_item_t;

static button_list_t s_button_list[BUTTON_MAX_NUM];
static button_item_t self;

static btn_id_e last_button_id = BTN_ID_MAX_NUMBER;
static btn_event_e last_button_event = BTN_EVENT_NONE;

static uint8_t button_flex_read(void *flex)
{
	flex_button_t *btn = (flex_button_t *)flex;
	button_list_t *list = (button_list_t *)btn->user_data;
	int val;

	if ((list == NULL) || (list->gpio == NULL))
	{
		return 0U;
	}

	/* DT ACTIVE_* 已编码：1 = 逻辑按下 */
	val = gpio_pin_get_dt(list->gpio);
	if (val < 0)
	{
		return 0U;
	}
	return (uint8_t)val;
}

static void button_flex_event_callback(void *arg)
{
	flex_button_t *flex = (flex_button_t *)arg;
	button_list_t *list = (button_list_t *)flex->user_data;
	flex_button_event_t fevt = flex_button_event_read(flex);
	btn_event_e bevt = BTN_EVENT_NONE;

	switch (fevt)
	{
	case FLEX_BTN_PRESS_DOWN:
		bevt = BTN_EVENT_PRESS_DOWN;
		break;
	case FLEX_BTN_PRESS_CLICK:
		bevt = BTN_EVENT_SINGLE_CLICK;
		break;
	case FLEX_BTN_PRESS_DOUBLE_CLICK:
		bevt = BTN_EVENT_DOUBLE_CLICK;
		break;
	case FLEX_BTN_PRESS_REPEAT_CLICK:
		bevt = BTN_EVENT_REPEAT_CLICK;
		break;
	case FLEX_BTN_PRESS_LONG_START:
		bevt = BTN_EVENT_LONG_PRESS;
		break;
	case FLEX_BTN_PRESS_LONG_HOLD:
		bevt = BTN_EVENT_LONG_HOLD;
		break;
	case FLEX_BTN_PRESS_LONG_HOLD_UP:
		bevt = BTN_EVENT_LONG_HOLD_UP;
		break;
	default:
		bevt = BTN_EVENT_NONE;
		break;
	}

	if (bevt == BTN_EVENT_NONE)
	{
		return;
	}

	last_button_id = list->id;
	last_button_event = bevt;
	event_set(EVT_ID_BUTTON);

	if (self.notify != NULL)
	{
		self.notify(list->id, list->name, (btn_permission_e)list->permission, bevt);
	}
}

static void button_flex_init(button_list_t *list)
{
	memset(&list->flex, 0, sizeof(flex_button_t));
	list->flex.usr_button_read = button_flex_read;
	list->flex.cb = button_flex_event_callback;
	/* gpio_pin_get_dt：按下 = 1 */
	list->flex.pressed_logic_level = 1U;
	list->flex.debounce_tick = FLEX_MS_TO_SCAN_CNT(80);
	list->flex.max_multiple_clicks_interval = FLEX_MS_TO_SCAN_CNT(600);
	list->flex.short_press_start_tick = FLEX_MS_TO_SCAN_CNT(2000);
	list->flex.long_press_start_tick = FLEX_MS_TO_SCAN_CNT(10000);
	list->flex.long_hold_start_tick = FLEX_MS_TO_SCAN_CNT(11000);
	list->flex.id = (uint8_t)list->id;
	list->flex.user_data = list;

	(void)flex_button_register(&list->flex);
}

static status_t button_load_from_board(void)
{
	uint8_t n = board_button_count();

	if (n == 0U)
	{
		return STATUS_NOT_SUPPORTED;
	}
	if (n > BUTTON_MAX_NUM)
	{
		n = BUTTON_MAX_NUM;
	}

	self.num = 0U;
	for (uint8_t i = 0; i < n; i++)
	{
		const board_button_spec_t *spec = board_button_spec(i);
		button_list_t *entry = &s_button_list[self.num];

		if ((spec == NULL) || (spec->gpio == NULL))
		{
			continue;
		}
		if (!gpio_is_ready_dt(spec->gpio))
		{
			LOG_WARN("button: gpio not ready id=%u name=%s", spec->id,
				 spec->name != NULL ? spec->name : "?");
			continue;
		}
		if (gpio_pin_configure_dt(spec->gpio, GPIO_INPUT) != 0)
		{
			LOG_WARN("button: configure fail id=%u", spec->id);
			continue;
		}

		entry->id = (btn_id_e)spec->id;
		entry->name = spec->name;
		entry->gpio = spec->gpio;
		entry->active_level = spec->active_level;
		entry->permission = spec->permission;
		button_flex_init(entry);
		self.num++;
	}

	self.list = s_button_list;
	return (self.num > 0U) ? STATUS_OK : STATUS_FAIL;
}

status_t button_init(btn_notify_t notify)
{
	memset(&self, 0, sizeof(button_item_t));
	self.notify = notify;
	last_button_id = BTN_ID_MAX_NUMBER;
	last_button_event = BTN_EVENT_NONE;

	if (!board_wants(BOARD_MASK_BUTTON))
	{
		return STATUS_NOT_SUPPORTED;
	}

	if (button_load_from_board() != STATUS_OK)
	{
		return STATUS_FAIL;
	}

	self.ready = true;
	LOG_INFO("button: init ok keys=%u", self.num);
	return STATUS_OK;
}

void button_deinit(void)
{
	memset(&self, 0, sizeof(button_item_t));
	last_button_id = BTN_ID_MAX_NUMBER;
	last_button_event = BTN_EVENT_NONE;
}

void button_schedule(void)
{
	if (!self.ready || (self.list == NULL) || (self.num == 0U))
	{
		return;
	}
	(void)flex_button_scan();
}

static void button_wake_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	event_set_from_isr(EVT_ID_BUTTON);
}

void button_wake_enable(void)
{
	uint8_t i;
	uint32_t pin_mask = 0U;
	const struct device *port = NULL;

	if (!self.ready || (self.list == NULL) || (self.num == 0U))
	{
		return;
	}

	for (i = 0U; i < self.num; i++)
	{
		button_list_t *p = &self.list[i];

		if ((p->gpio == NULL) || ((p->permission & (uint16_t)BTN_PERMISSION_WAKE) == 0U))
		{
			continue;
		}
		if (!device_is_ready(p->gpio->port))
		{
			continue;
		}
		if (port == NULL)
		{
			port = p->gpio->port;
		}
		else if (port != p->gpio->port)
		{
			LOG_WARN("button: wake IRQ skip %s (other port)",
				 p->name != NULL ? p->name : "?");
			continue;
		}
		pin_mask |= BIT(p->gpio->pin);
		(void)gpio_pin_interrupt_configure_dt(p->gpio, GPIO_INT_EDGE_TO_ACTIVE);
	}

	if ((port == NULL) || (pin_mask == 0U))
	{
		LOG_WARN("button: no WAKE permission keys");
		return;
	}

	if (!self.wake_cb_ready)
	{
		gpio_init_callback(&self.wake_cb, button_wake_isr, pin_mask);
		(void)gpio_add_callback(port, &self.wake_cb);
		self.wake_cb_ready = true;
	}

	self.wake_irq_on = true;
}

void button_wake_disable(void)
{
	uint8_t i;

	if (!self.ready || (self.list == NULL))
	{
		return;
	}

	for (i = 0U; i < self.num; i++)
	{
		button_list_t *p = &self.list[i];

		if ((p->gpio == NULL) || ((p->permission & (uint16_t)BTN_PERMISSION_WAKE) == 0U))
		{
			continue;
		}
		(void)gpio_pin_interrupt_configure_dt(p->gpio, GPIO_INT_DISABLE);
	}

	self.wake_irq_on = false;
}

void button_last_event_get(btn_id_e *id, btn_event_e *event)
{
	if (id != NULL)
	{
		*id = last_button_id;
	}
	if (event != NULL)
	{
		*event = last_button_event;
	}
}

void button_last_event_clear(void)
{
	last_button_id = BTN_ID_MAX_NUMBER;
	last_button_event = BTN_EVENT_NONE;
	event_clear(EVT_ID_BUTTON);
}

const char *button_id_to_str(btn_id_e id)
{
	if (self.list != NULL)
	{
		for (uint8_t i = 0; i < self.num; i++)
		{
			if ((self.list[i].id == id) && (self.list[i].name != NULL))
			{
				return self.list[i].name;
			}
		}
	}

	switch (id)
	{
	case BTN_ID_UP:
		return "up";
	case BTN_ID_DOWN:
		return "down";
	case BTN_ID_LEFT:
		return "left";
	case BTN_ID_RIGHT:
		return "right";
	case BTN_ID_CONFIRM:
		return "confirm";
	case BTN_ID_BACK:
		return "back";
	default:
		return "unknown";
	}
}

const char *button_event_to_str(btn_event_e event)
{
	switch (event)
	{
	case BTN_EVENT_NONE:
		return "NONE";
	case BTN_EVENT_PRESS_DOWN:
		return "PRESS_DOWN";
	case BTN_EVENT_PRESS_UP:
		return "PRESS_UP";
	case BTN_EVENT_SINGLE_CLICK:
		return "SINGLE_CLICK";
	case BTN_EVENT_DOUBLE_CLICK:
		return "DOUBLE_CLICK";
	case BTN_EVENT_REPEAT_CLICK:
		return "REPEAT_CLICK";
	case BTN_EVENT_LONG_PRESS:
		return "LONG_PRESS";
	case BTN_EVENT_LONG_HOLD:
		return "LONG_HOLD";
	case BTN_EVENT_LONG_HOLD_UP:
		return "LONG_HOLD_UP";
	default:
		return "UNKNOWN";
	}
}

void button_log_notify(btn_id_e id, const char *name, btn_permission_e permission, btn_event_e event)
{
	(void)permission;

	if (event == BTN_EVENT_SINGLE_CLICK || event == BTN_EVENT_DOUBLE_CLICK ||
	    event == BTN_EVENT_REPEAT_CLICK || event == BTN_EVENT_LONG_PRESS ||
	    event == BTN_EVENT_LONG_HOLD || event == BTN_EVENT_LONG_HOLD_UP)
	{
		LOG_INFO("button id=%d(%s) name=%s event=%s", (int)id, button_id_to_str(id),
			 name != NULL ? name : "NULL", button_event_to_str(event));
	}
}

uint8_t button_get_level(btn_id_e id)
{
	if ((self.list == NULL) || !self.ready)
	{
		return 0xFFU;
	}

	for (uint8_t i = 0; i < self.num; i++)
	{
		button_list_t *p = &self.list[i];

		if (p->id == id)
		{
			return button_flex_read(&p->flex);
		}
	}
	return 0xFFU;
}
