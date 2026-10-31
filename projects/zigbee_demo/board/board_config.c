/**
 * @file board_config.c
 * @brief zigbee_demo — eink_tag rev_b?UART + LED×2 + BTN×2?? SPI/BLE?
 */

#include "board.h"
#include "button.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define ZUSER DT_PATH(zephyr_user)

static const struct gpio_dt_spec s_led_g = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec s_led_b = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec s_btn1 = GPIO_DT_SPEC_GET(ZUSER, btn1_gpios);
static const struct gpio_dt_spec s_btn2 = GPIO_DT_SPEC_GET(ZUSER, btn2_gpios);

static const board_led_spec_t s_leds[] = {
	{.gpio = &s_led_g, .id = 0U, .name = "led_g"},
	{.gpio = &s_led_b, .id = 1U, .name = "led_b"},
};

static const board_uart_spec_t s_uarts[] = {
	{
		.dev = DEVICE_DT_GET(DT_NODELABEL(uart20)),
		.baud = 115200U,
		.name = "console",
	},
};

static const board_button_spec_t s_buttons[] = {
	{
		.gpio = &s_btn1,
		.id = (uint8_t)BTN_ID_CONFIRM,
		.active_level = 0U,
		.permission = (uint16_t)BTN_PERMISSION_CONFIRM,
		.name = "btn1",
	},
	{
		.gpio = &s_btn2,
		.id = (uint8_t)BTN_ID_BACK,
		.active_level = 0U,
		.permission = (uint16_t)BTN_PERMISSION_BACK,
		.name = "btn2",
	},
};

static const board_config_t s_board = {
	.name = "zigbee_demo_rev_b",
	.mask = BOARD_MASK_UART | BOARD_MASK_LED | BOARD_MASK_BUTTON,

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

	.buttons = s_buttons,
	.button_count = (uint8_t)ARRAY_SIZE(s_buttons),

	.irqs = NULL,
	.irq_count = 0U,
};

const board_config_t *board_config_get(void)
{
	return &s_board;
}
