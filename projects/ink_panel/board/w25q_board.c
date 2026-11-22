/**
 * @file w25q_board.c
 * @brief ink_panel：W25Q64 — SPIM00 + 软件 CS；回调注入 cbb/w25q
 *
 * 禁止在 ISR/GATT 回调中调用。
 */

#include "w25q_board.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define ZUSER DT_PATH(zephyr_user)

/*
 * SPIM00 max-frequency 通常更高；Mode0 @ 8 MHz（与 Nordic P2 loopback 同脚）。
 * PRESCALER 取偶，避开 Mode0 且 PRESCALER>2 的 errata 写路径。
 */
#define W25Q_SPI_HZ     8000000U
#define W25Q_SPI_CHUNK  64U

w25q_t g_w25q;

static const struct device *const s_spi = DEVICE_DT_GET(DT_NODELABEL(spi00));
static const struct gpio_dt_spec s_cs =
	GPIO_DT_SPEC_GET(ZUSER, flash_cs_gpios);
static const struct gpio_dt_spec s_wp =
	GPIO_DT_SPEC_GET(ZUSER, flash_wp_gpios);
static const struct gpio_dt_spec s_hold =
	GPIO_DT_SPEC_GET(ZUSER, flash_hold_gpios);

static struct spi_config s_spi_cfg;
static uint8_t s_tx_chunk[W25Q_SPI_CHUNK];
static uint8_t s_rx_chunk[W25Q_SPI_CHUNK];
static uint32_t s_spi_err_count;

static void w25q_board_set_cs(uint8_t level)
{
	/* level 0=选中(CS 低)，1=释放；脚为 ACTIVE_LOW 规格，用 raw 对齐 epd */
	(void)gpio_pin_set_raw(s_cs.port, s_cs.pin, level ? 1 : 0);
}

static void w25q_board_delay_ms(uint32_t ms)
{
	k_msleep(ms);
}

static uint32_t w25q_board_get_tick_ms(void)
{
	return k_uptime_get_32();
}

static int w25q_board_spi_xfer(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
	uint16_t off = 0U;

	if ((tx == NULL) || (len == 0U)) {
		return -1;
	}
	if (!device_is_ready(s_spi)) {
		return -1;
	}

	while (off < len) {
		uint16_t n = (uint16_t)(len - off);
		struct spi_buf tx_buf;
		struct spi_buf rx_buf;
		const struct spi_buf_set tx_set = {
			.buffers = &tx_buf,
			.count = 1U,
		};
		const struct spi_buf_set rx_set = {
			.buffers = &rx_buf,
			.count = 1U,
		};
		int err;

		if (n > W25Q_SPI_CHUNK) {
			n = W25Q_SPI_CHUNK;
		}

		memcpy(s_tx_chunk, &tx[off], n);
		tx_buf.buf = s_tx_chunk;
		tx_buf.len = n;

		if (rx != NULL) {
			rx_buf.buf = s_rx_chunk;
			rx_buf.len = n;
			err = spi_transceive(s_spi, &s_spi_cfg, &tx_set, &rx_set);
			if (err == 0) {
				memcpy(&rx[off], s_rx_chunk, n);
			}
		} else {
			err = spi_write(s_spi, &s_spi_cfg, &tx_set);
		}

		if (err != 0) {
			s_spi_err_count++;
			if (s_spi_err_count <= 3U) {
				printk("w25q: spi err=%d\n", err);
			}
			return -1;
		}
		off = (uint16_t)(off + n);
	}

	return 0;
}

static int w25q_board_gpio_init(void)
{
	if (!gpio_is_ready_dt(&s_cs) || !gpio_is_ready_dt(&s_wp) ||
	    !gpio_is_ready_dt(&s_hold)) {
		printk("w25q: gpio not ready\n");
		return -1;
	}

	/* CS 闲高；WP/HOLD 驱高（允许标准 SPI 读写） */
	if (gpio_pin_configure_dt(&s_cs, GPIO_OUTPUT_INACTIVE) != 0) {
		printk("w25q: CS cfg fail\n");
		return -1;
	}
	if (gpio_pin_configure_dt(&s_wp, GPIO_OUTPUT_ACTIVE) != 0) {
		printk("w25q: WP cfg fail\n");
		return -1;
	}
	if (gpio_pin_configure_dt(&s_hold, GPIO_OUTPUT_ACTIVE) != 0) {
		printk("w25q: HOLD cfg fail\n");
		return -1;
	}

	w25q_board_set_cs(1U);
	return 0;
}

int w25q_board_init(void)
{
	w25q_config_t cfg = {
		.spi_xfer = w25q_board_spi_xfer,
		.set_cs = w25q_board_set_cs,
		.delay_ms = w25q_board_delay_ms,
		.get_tick_ms = w25q_board_get_tick_ms,
		.read_stream = NULL,
		.skip_id_check = false,
	};
	w25q_status_t st;

	if (!device_is_ready(s_spi)) {
		printk("w25q: spi00 not ready\n");
		return -1;
	}

	if (w25q_board_gpio_init() != 0) {
		return -1;
	}

	s_spi_cfg.frequency = W25Q_SPI_HZ;
	s_spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
	s_spi_cfg.slave = 0;
	s_spi_cfg.cs.gpio.port = NULL;

	k_msleep(2);

	st = w25q_init(&g_w25q, &cfg);
	if (st != W25Q_OK) {
		printk("w25q: init fail st=%d\n", (int)st);
		return -1;
	}

	return 0;
}
