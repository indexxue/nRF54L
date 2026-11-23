/**
 * @file board_config.c
 * @brief ink_panel — EVT ?????UART + LED×1 + BTN×1
 */

#include "board.h"
#include "button.h"

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#define ZUSER DT_PATH(zephyr_user)

static const struct gpio_dt_spec s_led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec s_btn1 = GPIO_DT_SPEC_GET(ZUSER, btn1_gpios);

static const board_led_spec_t s_leds[] = {
	{.gpio = &s_led0, .id = 0U, .name = "led_status"},
};

/* uart00 ?? spi00?W25Q????? RTT????? UART */
static const board_button_spec_t s_buttons[] = {
	{
		.gpio = &s_btn1,
		.id = (uint8_t)BTN_ID_CONFIRM,
		.active_level = 0U,
		.permission = (uint16_t)(BTN_PERMISSION_CONFIRM | BTN_PERMISSION_WAKE),
		.name = "btn1",
	},
};

static const board_config_t s_board = {
	.name = "ink_panel",
	.mask = BOARD_MASK_LED | BOARD_MASK_BUTTON | BOARD_MASK_BLE,

	.leds = s_leds,
	.led_count = (uint8_t)ARRAY_SIZE(s_leds),

	.uarts = NULL,
	.uart_count = 0U,

	.i2cs = NULL,
	.i2c_count = 0U,

	.spi_buses = NULL,
	.spi_bus_count = 0U,
	.spi_slaves = NULL,
	.spi_slave_count = 0U,

	.adcs = NULL,
	.adc_count = 0U,
	.pwms = NULL,
	.pwm_count = 0U,

	.buttons = s_buttons,
	.button_count = (uint8_t)ARRAY_SIZE(s_buttons),
};

const board_config_t *board_config_get(void)
{
	return &s_board;
}
