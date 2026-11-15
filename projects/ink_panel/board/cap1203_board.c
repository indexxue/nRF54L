/**
 * @file cap1203_board.c
 * @brief ink_panel：CAP1203 — ALERT ISR ingress + work 读触摸（禁止在 ISR 里 I2C）
 */

#include "cap1203_board.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define ZUSER DT_PATH(zephyr_user)

/* Averaging and Sampling Config：压到最快档，减轻“按了半天才报” */
#define CAP1203_REG_AVG_SAMP_CFG 0x24u
#define CAP1203_AVG_SAMP_FAST	 0x00u /* AVG=1, 最短采样/周期 */

cap1203_t g_cap1203;

static const struct gpio_dt_spec s_alert =
	GPIO_DT_SPEC_GET(ZUSER, touch_alert_gpios);

static struct gpio_callback s_alert_cb;
static struct k_work s_touch_work;
static struct k_work_delayable s_release_work;

static uint8_t s_last_mask;
static bool s_have_last;
static bool s_irq_ok;

static const struct device *cap1203_board_i2c(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(i2c21));
}

static int cap1203_board_write(uint8_t addr7, const uint8_t *data, uint16_t len)
{
	const struct device *i2c = cap1203_board_i2c();

	if (!device_is_ready(i2c) || data == NULL || len == 0U) {
		return -1;
	}
	return (i2c_write(i2c, data, len, addr7) == 0) ? 0 : -1;
}

static int cap1203_board_write_read(uint8_t addr7, const uint8_t *tx, uint16_t tx_len,
				    uint8_t *rx, uint16_t rx_len)
{
	const struct device *i2c = cap1203_board_i2c();

	if (!device_is_ready(i2c) || tx == NULL || rx == NULL || tx_len == 0U ||
	    rx_len == 0U) {
		return -1;
	}
	return (i2c_write_read(i2c, addr7, tx, tx_len, rx, rx_len) == 0) ? 0 : -1;
}

static void cap1203_board_delay_ms(uint32_t ms)
{
	k_msleep(ms);
}

static void cap1203_board_touch_handle(void)
{
	uint8_t mask = 0U;

	if (!g_cap1203.initialized) {
		return;
	}
	if (cap1203_read_touched(&g_cap1203, &mask) != CAP1203_OK) {
		return;
	}

	if (!s_have_last || mask != s_last_mask) {
		printk("cap1203 touch=0x%02x cs1=%d cs2=%d cs3=%d\n", mask,
		       (mask & CAP1203_CS1) ? 1 : 0, (mask & CAP1203_CS2) ? 1 : 0,
		       (mask & CAP1203_CS3) ? 1 : 0);
		s_last_mask = mask;
		s_have_last = true;
	}

	(void)cap1203_clear_int(&g_cap1203);

	/* 按住期间芯片不一定再拉 ALERT；短延迟复读抓松手 */
	if (mask != 0U) {
		(void)k_work_reschedule(&s_release_work, K_MSEC(30));
	} else {
		(void)k_work_cancel_delayable(&s_release_work);
	}
}

static void cap1203_board_touch_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	cap1203_board_touch_handle();
}

static void cap1203_board_release_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	cap1203_board_touch_handle();
}

/** ISR：只入队，禁止 I2C / printk / sleep */
static void cap1203_board_alert_isr(const struct device *port,
				    struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);
	(void)k_work_submit(&s_touch_work);
}

static int cap1203_board_irq_init(void)
{
	int ret;

	if (!gpio_is_ready_dt(&s_alert)) {
		printk("cap1203 alert gpio fail\n");
		return -1;
	}

	ret = gpio_pin_configure_dt(&s_alert, GPIO_INPUT);
	if (ret != 0) {
		return -2;
	}
	ret = gpio_pin_interrupt_configure_dt(&s_alert, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("cap1203 alert irq cfg=%d\n", ret);
		return -3;
	}

	gpio_init_callback(&s_alert_cb, cap1203_board_alert_isr, BIT(s_alert.pin));
	ret = gpio_add_callback(s_alert.port, &s_alert_cb);
	if (ret != 0) {
		return -4;
	}

	s_irq_ok = true;
	return 0;
}

int cap1203_board_bringup(void)
{
	const struct device *i2c = cap1203_board_i2c();
	cap1203_config_t cfg = {
		.write = cap1203_board_write,
		.write_read = cap1203_board_write_read,
		.delay_ms = cap1203_board_delay_ms,
		.i2c_addr = CAP1203_ADDR_7BIT_DEFAULT,
	};
	cap1203_setup_t setup = {
		.sensor_enable = CAP1203_CS1 | CAP1203_CS2,
		.interrupt_enable = CAP1203_CS1 | CAP1203_CS2,
		.delta_sense = 1u,
		.disable_multitouch_block = true,
	};
	cap1203_status_t st;

	s_have_last = false;
	s_last_mask = 0U;
	s_irq_ok = false;

	k_work_init(&s_touch_work, cap1203_board_touch_work_fn);
	k_work_init_delayable(&s_release_work, cap1203_board_release_work_fn);

	if (!device_is_ready(i2c)) {
		printk("cap1203 i2c fail\n");
		return -1;
	}

	st = cap1203_init(&g_cap1203, &cfg);
	if (st != CAP1203_OK) {
		printk("cap1203 init=%d\n", (int)st);
		return -2;
	}

	st = cap1203_configure(&g_cap1203, &setup);
	if (st != CAP1203_OK) {
		printk("cap1203 cfg=%d\n", (int)st);
		return -3;
	}

	st = cap1203_reg_write(&g_cap1203, CAP1203_REG_AVG_SAMP_CFG, CAP1203_AVG_SAMP_FAST);
	if (st != CAP1203_OK) {
		printk("cap1203 avg=%d\n", (int)st);
		return -4;
	}

	if (cap1203_board_irq_init() != 0) {
		printk("cap1203 irq fail (fallback poll)\n");
	} else {
		printk("cap1203 ok cs1+cs2 irq\n");
	}

	/* 起机清一次；若 ALERT 已低则补一次处理 */
	(void)cap1203_clear_int(&g_cap1203);
	(void)k_work_submit(&s_touch_work);
	return 0;
}

void cap1203_board_poll(void)
{
	/* 无 IRQ 时的兜底；有 IRQ 时也可被 main 低频调用 */
	if (!s_irq_ok) {
		cap1203_board_touch_handle();
	}
}
