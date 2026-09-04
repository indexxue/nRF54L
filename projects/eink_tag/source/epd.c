/**
 * @file epd.c
 * @brief 墨水屏胶水：SPI 从机名 "epd"；DC/RST/BUSY 本文件读 DT。
 */

#include "epd.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>

#include "board.h"
#include "log.h"

#define ZUSER DT_PATH(zephyr_user)

static const struct gpio_dt_spec s_dc = GPIO_DT_SPEC_GET(ZUSER, epd_dc_gpios);
static const struct gpio_dt_spec s_rst = GPIO_DT_SPEC_GET(ZUSER, epd_rst_gpios);
static const struct gpio_dt_spec s_busy = GPIO_DT_SPEC_GET(ZUSER, epd_busy_gpios);

static const board_spi_slave_spec_t *epd_find_slave(void)
{
	const uint8_t n = board_spi_slave_count();

	for (uint8_t i = 0; i < n; i++)
	{
		const board_spi_slave_spec_t *s = board_spi_slave_spec(i);

		if ((s != NULL) && (s->name != NULL) && (strcmp(s->name, "epd") == 0))
		{
			return s;
		}
	}
	return NULL;
}

status_t epd_init(void)
{
	const board_spi_slave_spec_t *slave;
	const board_spi_bus_spec_t *bus;

	slave = epd_find_slave();
	if (slave == NULL)
	{
		LOG_ERROR("epd: spi slave \"epd\" missing in board_config");
		return STATUS_INVALID_STATE;
	}

	bus = board_spi_bus_spec(slave->bus_index);
	if (bus == NULL)
	{
		LOG_ERROR("epd: spi bus %u missing", slave->bus_index);
		return STATUS_FAIL;
	}

	if ((slave->cs == NULL) || !gpio_is_ready_dt(slave->cs) || !gpio_is_ready_dt(&s_dc) ||
	    !gpio_is_ready_dt(&s_rst) || !gpio_is_ready_dt(&s_busy))
	{
		LOG_ERROR("epd: GPIO not ready");
		return STATUS_FAIL;
	}

	if (gpio_pin_configure_dt(slave->cs, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_dc, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_rst, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_busy, GPIO_INPUT) != 0)
	{
		return STATUS_FAIL;
	}

	LOG_INFO("epd: slave ok bus=%s cs=%s bitrate=%u (cbb TBD)", bus->name, slave->name,
		 (unsigned int)(slave->bitrate_hz != 0U ? slave->bitrate_hz : bus->bitrate_hz));
	return STATUS_OK;
}
