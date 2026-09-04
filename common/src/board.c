/**
 * @file board.c
 * @brief 按 board_config_t 查询 / 初始化 SoC 外设规格表。
 */

#include "board.h"

#include "led_scene.h"

bool board_wants(uint32_t mask)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || (mask == 0U))
	{
		return false;
	}
	return (cfg->mask & mask) == mask;
}

uint8_t board_led_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_LED))
	{
		return 0U;
	}
	return cfg->led_count;
}

const board_led_spec_t *board_led_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_LED) || (cfg->leds == NULL) ||
	    (index >= cfg->led_count))
	{
		return NULL;
	}
	return &cfg->leds[index];
}

uint8_t board_uart_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_UART))
	{
		return 0U;
	}
	return cfg->uart_count;
}

const board_uart_spec_t *board_uart_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_UART) || (cfg->uarts == NULL) ||
	    (index >= cfg->uart_count))
	{
		return NULL;
	}
	return &cfg->uarts[index];
}

uint8_t board_i2c_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_I2C))
	{
		return 0U;
	}
	return cfg->i2c_count;
}

const board_i2c_spec_t *board_i2c_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_I2C) || (cfg->i2cs == NULL) ||
	    (index >= cfg->i2c_count))
	{
		return NULL;
	}
	return &cfg->i2cs[index];
}

uint8_t board_spi_bus_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_SPI))
	{
		return 0U;
	}
	return cfg->spi_bus_count;
}

const board_spi_bus_spec_t *board_spi_bus_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_SPI) || (cfg->spi_buses == NULL) ||
	    (index >= cfg->spi_bus_count))
	{
		return NULL;
	}
	return &cfg->spi_buses[index];
}

uint8_t board_spi_slave_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_SPI))
	{
		return 0U;
	}
	return cfg->spi_slave_count;
}

const board_spi_slave_spec_t *board_spi_slave_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_SPI) || (cfg->spi_slaves == NULL) ||
	    (index >= cfg->spi_slave_count))
	{
		return NULL;
	}
	return &cfg->spi_slaves[index];
}

uint8_t board_adc_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_ADC))
	{
		return 0U;
	}
	return cfg->adc_count;
}

const board_adc_spec_t *board_adc_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_ADC) || (cfg->adcs == NULL) ||
	    (index >= cfg->adc_count))
	{
		return NULL;
	}
	return &cfg->adcs[index];
}

uint8_t board_input_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_INPUT))
	{
		return 0U;
	}
	return cfg->input_count;
}

const board_input_spec_t *board_input_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_INPUT) || (cfg->inputs == NULL) ||
	    (index >= cfg->input_count))
	{
		return NULL;
	}
	return &cfg->inputs[index];
}

uint8_t board_pwm_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_PWM))
	{
		return 0U;
	}
	return cfg->pwm_count;
}

const board_pwm_spec_t *board_pwm_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_PWM) || (cfg->pwms == NULL) ||
	    (index >= cfg->pwm_count))
	{
		return NULL;
	}
	return &cfg->pwms[index];
}

uint8_t board_button_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_BUTTON))
	{
		return 0U;
	}
	return cfg->button_count;
}

const board_button_spec_t *board_button_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_BUTTON) || (cfg->buttons == NULL) ||
	    (index >= cfg->button_count))
	{
		return NULL;
	}
	return &cfg->buttons[index];
}

uint8_t board_irq_count(void)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_IRQ))
	{
		return 0U;
	}
	return cfg->irq_count;
}

const board_irq_spec_t *board_irq_spec(uint8_t index)
{
	const board_config_t *cfg = board_config_get();

	if ((cfg == NULL) || !board_wants(BOARD_MASK_IRQ) || (cfg->irqs == NULL) ||
	    (index >= cfg->irq_count))
	{
		return NULL;
	}
	return &cfg->irqs[index];
}

status_t board_init(void)
{
	const board_config_t *cfg = board_config_get();

	if (cfg == NULL)
	{
		return STATUS_INVALID_STATE;
	}

	if (board_wants(BOARD_MASK_LED))
	{
		if (led_scene_init() != STATUS_OK)
		{
			return STATUS_FAIL;
		}
	}

	return STATUS_OK;
}
