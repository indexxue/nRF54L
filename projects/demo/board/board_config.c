/**
 * @file board_config.c
 * @brief demo — UART×1 + LED×2；其余个数为 0。
 */

#include "board.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

static const struct gpio_dt_spec s_led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec s_led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);

static const board_led_spec_t s_leds[] = {
	{.gpio = &s_led0, .id = 0U, .name = "led0"},
	{.gpio = &s_led1, .id = 1U, .name = "led1"},
};

static const board_uart_spec_t s_uarts[] = {
	{
		.dev = DEVICE_DT_GET(DT_NODELABEL(uart20)),
		.baud = 115200U,
		.name = "console",
	},
};

static const board_config_t s_board = {
	.name = "demo_ty_rev_a",
	.mask = BOARD_MASK_LED | BOARD_MASK_UART,

	.leds = s_leds,
	.led_count = (uint8_t)ARRAY_SIZE(s_leds),

	.uarts = s_uarts,
	.uart_count = (uint8_t)ARRAY_SIZE(s_uarts),

	.i2cs = NULL,
	.i2c_count = 0U,

	.spi_buses = NULL,
	.spi_bus_count = 0U,
	.spi_slaves = NULL,
	.spi_slave_count = 0U,

	.adcs = NULL,
	.adc_count = 0U,
	.inputs = NULL,
	.input_count = 0U,
	.pwms = NULL,
	.pwm_count = 0U,
	.buttons = NULL,
	.button_count = 0U,
	.irqs = NULL,
	.irq_count = 0U,
};

const board_config_t *board_config_get(void)
{
	return &s_board;
}
