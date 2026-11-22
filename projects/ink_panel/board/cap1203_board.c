/**
 * @file cap1203_board.c
 * @brief ink_panel：CAP1203 — ALERT ISR ingress + work；按下边沿 → APP_EVT_TOUCH
 */

#include "cap1203_board.h"

#include "app_event.h"
#include "npm1300_board.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#define ZUSER DT_PATH(zephyr_user)

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
static bool s_ready;
static atomic_t s_nav_pending;
static uint8_t s_read_fail_streak;

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

static const char *cap1203_st_str(cap1203_status_t st)
{
	switch (st) {
	case CAP1203_OK:
		return "ok";
	case CAP1203_ERROR_PARAM:
		return "param";
	case CAP1203_ERROR_NOT_INIT:
		return "not_init";
	case CAP1203_ERROR_I2C:
		return "i2c(NACK/bus)";
	case CAP1203_ERROR_ID:
		return "bad_product_id";
	case CAP1203_ERROR_RANGE:
		return "range";
	default:
		return "?";
	}
}

static void cap1203_board_nav_post(uint8_t rising)
{
	uint8_t nav = 0U;

	if ((rising & CAP1203_CS1) != 0U) {
		nav |= CAP1203_NAV_L;
	}
	if ((rising & CAP1203_CS2) != 0U) {
		nav |= CAP1203_NAV_R;
	}
	if ((rising & CAP1203_CS3) != 0U) {
		nav |= CAP1203_NAV_HOME;
	}
	if (nav == 0U) {
		return;
	}

	(void)atomic_or(&s_nav_pending, nav);
	k_event_post(&app_events, APP_EVT_TOUCH);
}

static void cap1203_board_touch_handle(void)
{
	uint8_t mask = 0U;
	uint8_t rising;
	uint8_t falling;
	cap1203_status_t st;

	if (!s_ready || !g_cap1203.initialized) {
		return;
	}

	st = cap1203_read_touched(&g_cap1203, &mask);
	if (st != CAP1203_OK) {
		s_read_fail_streak++;
		/* 限流：首败 / 每 50 次打一行，避免刷屏关轨时刷屏日志 */
		if ((s_read_fail_streak == 1U) || ((s_read_fail_streak % 50U) == 0U)) {
			printk("cap1203: read fail st=%d (%s) n=%u\n", (int)st,
			       cap1203_st_str(st), (unsigned)s_read_fail_streak);
			npm1300_board_touch_rail_log("read-fail");
		}
		s_have_last = false;
		return;
	}
	s_read_fail_streak = 0U;

	if (!s_have_last) {
		/* 起机只建基线，避免上电误触导航 */
		s_last_mask = mask;
		s_have_last = true;
		printk("cap1203: baseline mask=0x%02x (L=%d R=%d H=%d)\n", mask,
		       (mask & CAP1203_CS1) ? 1 : 0, (mask & CAP1203_CS2) ? 1 : 0,
		       (mask & CAP1203_CS3) ? 1 : 0);
		rising = 0U;
		falling = 0U;
	} else {
		rising = (uint8_t)(mask & (uint8_t)~s_last_mask);
		falling = (uint8_t)(s_last_mask & (uint8_t)~mask);
		s_last_mask = mask;
	}

	if (rising != 0U) {
		printk("cap1203: CLICK down mask=0x%02x L=%d R=%d H=%d\n", rising,
		       (rising & CAP1203_CS1) ? 1 : 0, (rising & CAP1203_CS2) ? 1 : 0,
		       (rising & CAP1203_CS3) ? 1 : 0);
		cap1203_board_nav_post(rising);
	}
	if (falling != 0U) {
		printk("cap1203: CLICK up   mask=0x%02x L=%d R=%d H=%d\n", falling,
		       (falling & CAP1203_CS1) ? 1 : 0, (falling & CAP1203_CS2) ? 1 : 0,
		       (falling & CAP1203_CS3) ? 1 : 0);
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
		.sensor_enable = CAP1203_CS_ALL,
		.interrupt_enable = CAP1203_CS_ALL,
		.delta_sense = 1u,
		.disable_multitouch_block = true,
	};
	cap1203_status_t st = CAP1203_ERROR_I2C;
	int attempt;

	s_ready = false;
	s_have_last = false;
	s_last_mask = 0U;
	s_irq_ok = false;
	s_read_fail_streak = 0U;
	atomic_set(&s_nav_pending, 0);

	k_work_init(&s_touch_work, cap1203_board_touch_work_fn);
	k_work_init_delayable(&s_release_work, cap1203_board_release_work_fn);

	if (!device_is_ready(i2c)) {
		printk("cap1203 i2c device not ready\n");
		return -1;
	}

	/* 先开触摸电源并打轨日志，再 I2C 探 ID */
	printk("cap1203: power on before I2C…\n");
	npm1300_board_touch_power_set(true);
	npm1300_board_touch_rail_log("pre-delay");
	k_msleep(50);
	npm1300_board_touch_rail_log("pre-i2c");

	for (attempt = 1; attempt <= 3; attempt++) {
		st = cap1203_init(&g_cap1203, &cfg);
		if (st == CAP1203_OK) {
			break;
		}
		printk("cap1203 init try%d st=%d (%s) addr=0x%02x\n", attempt, (int)st,
		       cap1203_st_str(st), (unsigned)cfg.i2c_addr);
		npm1300_board_touch_rail_log("init-retry");
		npm1300_board_touch_power_set(true);
		k_msleep(30);
	}
	if (st != CAP1203_OK) {
		printk("cap1203 bringup fail: no ACK @0x28 — check VDD_Touch / ADDR\n");
		npm1300_board_touch_rail_log("init-fail");
		return -2;
	}

	st = cap1203_configure(&g_cap1203, &setup);
	if (st != CAP1203_OK) {
		printk("cap1203 cfg=%d (%s)\n", (int)st, cap1203_st_str(st));
		return -3;
	}

	st = cap1203_reg_write(&g_cap1203, CAP1203_REG_AVG_SAMP_CFG, CAP1203_AVG_SAMP_FAST);
	if (st != CAP1203_OK) {
		printk("cap1203 avg=%d (%s)\n", (int)st, cap1203_st_str(st));
		return -4;
	}

	if (cap1203_board_irq_init() != 0) {
		printk("cap1203 irq fail (fallback poll)\n");
	} else {
		printk("cap1203 ok cs1=L cs2=R cs3=HOME irq addr=0x28\n");
	}

	s_ready = true;
	npm1300_board_touch_rail_log("ready");
	(void)cap1203_clear_int(&g_cap1203);
	(void)k_work_submit(&s_touch_work);
	return 0;
}

void cap1203_board_poll(void)
{
	/* 有 IRQ 也轮询：丢边沿时仍能打 CLICK 日志并导航 */
	cap1203_board_touch_handle();
}

uint8_t cap1203_board_nav_take(void)
{
	return (uint8_t)atomic_set(&s_nav_pending, 0);
}
