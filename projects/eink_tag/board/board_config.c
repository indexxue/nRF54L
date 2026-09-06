/**
 * @file board_config.c
 * @brief eink_tag 板级：UART + LED×2 + BTN×2 + SPI00(EPD) + BLE；HFXO + LFCLK RC；NFC 暂关。
 *
 * | 资源   | 个数 | 接线                                      |
 * |--------|------|-------------------------------------------|
 * | UART   | 1    | TX=P1.09 RX=P1.10                         |
 * | LED    | 2    | G=P1.05 B=P1.04                           |
 * | BUTTON | 2    | BTN1=P0.00 BTN2=P0.01                     |
 * | SPI    | 1+1  | SCK=P2.01 MOSI=P2.04 CS=P2.05（从机 epd） |
 * | BLE    | 片上 | 2.4G RADIO（BOARD_MASK_BLE）              |
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
static const struct gpio_dt_spec s_epd_cs =
	GPIO_DT_SPEC_GET_BY_IDX(DT_NODELABEL(spi00), cs_gpios, 0);

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

static const board_spi_bus_spec_t s_spi_buses[] = {
	{
		.dev = DEVICE_DT_GET(DT_NODELABEL(spi00)),
		.bitrate_hz = 4000000U,
		.name = "spi00",
	},
};

static const board_spi_slave_spec_t s_spi_slaves[] = {
	{
		.bus_index = 0U,
		.cs = &s_epd_cs,
		.bitrate_hz = 4000000U,
		.name = "epd",
	},
};

static const board_config_t s_board = {
	.name = "eink_tag_rev_a",
	.mask = BOARD_MASK_UART | BOARD_MASK_LED | BOARD_MASK_BUTTON | BOARD_MASK_SPI |
		BOARD_MASK_BLE,

	.leds = s_leds,
	.led_count = (uint8_t)ARRAY_SIZE(s_leds),

	.uarts = s_uarts,
	.uart_count = (uint8_t)ARRAY_SIZE(s_uarts),

	.i2cs = NULL,
	.i2c_count = 0U,

	.spi_buses = s_spi_buses,
	.spi_bus_count = (uint8_t)ARRAY_SIZE(s_spi_buses),
	.spi_slaves = s_spi_slaves,
	.spi_slave_count = (uint8_t)ARRAY_SIZE(s_spi_slaves),

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
