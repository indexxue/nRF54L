/**
 * @file board.h
 * @brief 板级 **SoC 外设** 配置：掩码 + 规格表（含各资源个数）。各工程提供 `board_config_get()`。
 *
 * 个数约定：
 * - uart_count / i2c_count / pwm_count / button_count / … → 对应数组长度
 * - spi_bus_count → SPI **控制器/总线** 个数
 * - spi_slave_count → 总线上的 **从机** 个数（每条含 bus 下标 + CS；如墨水屏）
 *
 * SoC 射频类（BLE / NFC NFCT）仅用 mask，无从机表。
 * 片外器件（EPD 等）在工程 `source/`，挂 SPI/I2C；**勿把片上 NFC 建成 SPI 从机**。
 */

#ifndef COMMON_BOARD_H
#define COMMON_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#include "type.h"

/* -------------------------------------------------------------------------- */
/* SoC 外设掩码                                                                */
/* -------------------------------------------------------------------------- */

#define BOARD_MASK_LED    (1U << 0)
#define BOARD_MASK_UART   (1U << 1)
#define BOARD_MASK_I2C    (1U << 2)
#define BOARD_MASK_SPI    (1U << 3)
#define BOARD_MASK_ADC    (1U << 4)
#define BOARD_MASK_PWM    (1U << 5)
#define BOARD_MASK_BUTTON (1U << 6)
#define BOARD_MASK_BLE    (1U << 7)  /**< 片上 2.4G / BLE RADIO */
#define BOARD_MASK_NFC    (1U << 8)  /**< 片上 NFCT（Tag），非 SPI 外挂 */

#define BOARD_MASK_FULL                                                                                    \
	(BOARD_MASK_LED | BOARD_MASK_UART | BOARD_MASK_I2C | BOARD_MASK_SPI | BOARD_MASK_ADC |             \
	 BOARD_MASK_PWM | BOARD_MASK_BUTTON | BOARD_MASK_BLE | BOARD_MASK_NFC)

/* -------------------------------------------------------------------------- */
/* 规格表                                                                      */
/* -------------------------------------------------------------------------- */

typedef struct
{
	const struct gpio_dt_spec *gpio;
	uint8_t id;
	const char *name;
} board_led_spec_t;

typedef struct
{
	const struct device *dev;
	uint32_t baud;
	const char *name;
} board_uart_spec_t;

typedef struct
{
	const struct device *dev;
	uint32_t bitrate_hz;
	const char *name;
} board_i2c_spec_t;

/** SPI 控制器（总线） */
typedef struct
{
	const struct device *dev;
	uint32_t bitrate_hz;
	const char *name;
} board_spi_bus_spec_t;

/**
 * SPI 从机：挂在 spi_buses[bus_index] 上，CS 为独立 GPIO。
 * 片外器件逻辑仍在 source/；此处只登记板级接线个数与 CS。
 */
typedef struct
{
	uint8_t bus_index;
	const struct gpio_dt_spec *cs;
	uint32_t bitrate_hz; /**< 0 = 跟总线默认 */
	const char *name;
} board_spi_slave_spec_t;

typedef struct
{
	const struct device *dev;
	uint8_t channel;
	const char *name;
} board_adc_spec_t;

typedef struct
{
	const struct device *dev;
	uint32_t period_us;
	const char *name;
} board_pwm_spec_t;

typedef struct
{
	const struct gpio_dt_spec *gpio;
	uint8_t id;           /**< 建议对齐 btn_id_e */
	uint8_t active_level; /**< 物理有效电平；DT ACTIVE_* 时读脚用 gpio_pin_get_dt */
	uint16_t permission;  /**< 权限位，见 btn_permission_e */
	const char *name;
} board_button_spec_t;

typedef struct
{
	const char *name;
	uint32_t mask;

	const board_led_spec_t *leds;
	uint8_t led_count;

	const board_uart_spec_t *uarts;
	uint8_t uart_count;

	const board_i2c_spec_t *i2cs;
	uint8_t i2c_count;

	const board_spi_bus_spec_t *spi_buses;
	uint8_t spi_bus_count;

	const board_spi_slave_spec_t *spi_slaves;
	uint8_t spi_slave_count;

	const board_adc_spec_t *adcs;
	uint8_t adc_count;

	const board_pwm_spec_t *pwms;
	uint8_t pwm_count;

	const board_button_spec_t *buttons;
	uint8_t button_count;
} board_config_t;

const board_config_t *board_config_get(void);

bool board_wants(uint32_t mask);

uint8_t board_led_count(void);
const board_led_spec_t *board_led_spec(uint8_t index);

uint8_t board_uart_count(void);
const board_uart_spec_t *board_uart_spec(uint8_t index);

uint8_t board_i2c_count(void);
const board_i2c_spec_t *board_i2c_spec(uint8_t index);

uint8_t board_spi_bus_count(void);
const board_spi_bus_spec_t *board_spi_bus_spec(uint8_t index);

uint8_t board_spi_slave_count(void);
const board_spi_slave_spec_t *board_spi_slave_spec(uint8_t index);

uint8_t board_adc_count(void);
const board_adc_spec_t *board_adc_spec(uint8_t index);

uint8_t board_pwm_count(void);
const board_pwm_spec_t *board_pwm_spec(uint8_t index);

uint8_t board_button_count(void);
const board_button_spec_t *board_button_spec(uint8_t index);

status_t board_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_BOARD_H */
