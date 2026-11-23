/**
 * @file npm1300_board.c
 * @brief ink_panel：i2c21 → nPM1300 板级适配与产品电源策略
 *
 * 轨门控以 I2C LDSW 为准（默认关，须 TASK 使能）。MCU EN 脚同步驱动，
 * 供日后配 LDSWnGPISEL；当前未映射时单独拉 GPIO 不会出电压。
 */

#include "npm1300_board.h"

#include "led_scene.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define ZUSER DT_PATH(zephyr_user)

/* 与 cbb / PS 一致：VBUS 判定 */
#define NPM1300_BOARD_REG_VBUSINSTATUS0 0x0207u
#define NPM1300_BOARD_VBUS_DETECTED     0x01u /* VBUSINSTATUS bit0 */

#define NPM1300_BOARD_ICHG_MA  300u
#define NPM1300_BOARD_VTERM_MV 4200u

/* BCHGCHARGESTATUS */
#define NPM1300_CHG_BATTERYDETECTED   0x01u
#define NPM1300_CHG_COMPLETED         0x02u
#define NPM1300_CHG_TRICKLE           0x04u
#define NPM1300_CHG_CC                0x08u
#define NPM1300_CHG_CV                0x10u
#define NPM1300_CHG_ACTIVE \
	(NPM1300_CHG_TRICKLE | NPM1300_CHG_CC | NPM1300_CHG_CV)

#define NPM1300_BOARD_VBAT_EMPTY_MV 3300u
#define NPM1300_BOARD_VBAT_FULL_MV  4200u
#define NPM1300_BOARD_FULL_BLINK_S  60u
#define NPM1300_BOARD_FULL_PULSE_MS 200u

npm1300_t g_npm1300;

static const struct gpio_dt_spec s_epd_pwr =
	GPIO_DT_SPEC_GET(ZUSER, epd_pwr_gpios);
static const struct gpio_dt_spec s_touch_ldo =
	GPIO_DT_SPEC_GET(ZUSER, touch_ldo_gpios);
static const struct gpio_dt_spec s_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static bool s_charge_inhibit;
static bool s_charge_applied; /* 上次已成功使能充电 */
static bool s_gpio_ready;
static bool s_rails_cfg_ok;
static bool s_led_suppress; /* 唤醒场景占灯时禁充电直写 */

static uint16_t s_batt_mv;
static uint8_t s_batt_pct;
static uint8_t s_chg_status;
static bool s_batt_valid;
static bool s_vbus_cached;
static uint8_t s_full_blink_left; /* 满电闪烁倒计时（秒） */

static struct k_work_delayable s_charge_work;
static struct k_work_delayable s_led_pulse_work;
/* charge 周期任务与 EPD 供电同抢 I2C → 须持 s_npm_lock */
static K_MUTEX_DEFINE(s_npm_lock);

static const struct device *npm1300_board_i2c(void)
{
	return DEVICE_DT_GET(DT_NODELABEL(i2c21));
}

static int npm1300_board_write(uint8_t addr7, const uint8_t *data, uint16_t len)
{
	const struct device *i2c = npm1300_board_i2c();

	if (!device_is_ready(i2c) || data == NULL || len == 0U) {
		return -1;
	}
	return (i2c_write(i2c, data, len, addr7) == 0) ? 0 : -1;
}

static int npm1300_board_write_read(uint8_t addr7, const uint8_t *tx, uint16_t tx_len,
				    uint8_t *rx, uint16_t rx_len)
{
	const struct device *i2c = npm1300_board_i2c();

	if (!device_is_ready(i2c) || tx == NULL || rx == NULL || tx_len == 0U ||
	    rx_len == 0U) {
		return -1;
	}
	return (i2c_write_read(i2c, addr7, tx, tx_len, rx, rx_len) == 0) ? 0 : -1;
}

static void npm1300_board_delay_ms(uint32_t ms)
{
	k_msleep(ms);
}

static bool npm1300_board_vbus_present(void)
{
	uint8_t vbus = 0U;

	if (npm1300_reg_read(&g_npm1300, NPM1300_BOARD_REG_VBUSINSTATUS0, &vbus) !=
	    NPM1300_OK) {
		return false;
	}
	return (vbus & NPM1300_BOARD_VBUS_DETECTED) != 0U;
}

static int npm1300_board_charge_apply(bool want_on)
{
	npm1300_status_t st;

	if (!want_on) {
		st = npm1300_charger_enable(&g_npm1300, false);
		if (st != NPM1300_OK) {
			return -1;
		}
		s_charge_applied = false;
		return 0;
	}

	/* PS：改 ICHG 时充电器应先禁用 */
	st = npm1300_charger_enable(&g_npm1300, false);
	if (st != NPM1300_OK) {
		return -1;
	}
	st = npm1300_charger_set_ichg_ma(&g_npm1300, NPM1300_BOARD_ICHG_MA);
	if (st != NPM1300_OK) {
		return -2;
	}
	st = npm1300_charger_set_vterm_mv(&g_npm1300, NPM1300_BOARD_VTERM_MV);
	if (st != NPM1300_OK) {
		return -3;
	}
	st = npm1300_charger_enable(&g_npm1300, true);
	if (st != NPM1300_OK) {
		return -4;
	}
	s_charge_applied = true;
	return 0;
}

static uint8_t npm1300_board_vbat_to_pct(uint16_t mv)
{
	uint32_t span;
	uint32_t above;

	if (mv <= NPM1300_BOARD_VBAT_EMPTY_MV) {
		return 0U;
	}
	if (mv >= NPM1300_BOARD_VBAT_FULL_MV) {
		return 100U;
	}
	span = (uint32_t)(NPM1300_BOARD_VBAT_FULL_MV - NPM1300_BOARD_VBAT_EMPTY_MV);
	above = (uint32_t)(mv - NPM1300_BOARD_VBAT_EMPTY_MV);
	return (uint8_t)((above * 100U) / span);
}

static void npm1300_board_led_set(bool on)
{
	if (gpio_is_ready_dt(&s_led)) {
		(void)gpio_pin_set_dt(&s_led, on ? 1 : 0); /* DT ACTIVE_LOW */
	}
}

static void npm1300_board_led_pulse_off_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	if (s_led_suppress) {
		return;
	}
	/* 满电脉冲结束：熄灭（仍插电且满则等下一轮） */
	npm1300_board_led_set(false);
}

static void npm1300_board_led_update(bool vbus, bool charging, bool full)
{
	(void)k_work_cancel_delayable(&s_led_pulse_work);

	if (s_led_suppress) {
		return;
	}

	if (!vbus) {
		s_full_blink_left = 0U;
		npm1300_board_led_set(false);
		return;
	}

	if (charging) {
		s_full_blink_left = 0U;
		npm1300_board_led_set(true); /* 充电常亮 */
		return;
	}

	if (full) {
		if (s_full_blink_left == 0U) {
			npm1300_board_led_set(true);
			(void)k_work_reschedule(&s_led_pulse_work,
						 K_MSEC(NPM1300_BOARD_FULL_PULSE_MS));
			s_full_blink_left = NPM1300_BOARD_FULL_BLINK_S;
		} else {
			s_full_blink_left--;
			npm1300_board_led_set(false);
		}
		return;
	}

	/* 有 VBUS 但未在充也未满（如 inhibit）→ 灭 */
	s_full_blink_left = 0U;
	npm1300_board_led_set(false);
}

static void npm1300_board_batt_sample_locked(void)
{
	uint16_t mv = 0U;
	uint8_t st = 0U;
	npm1300_status_t adc_st;
	npm1300_status_t chg_st;

	adc_st = npm1300_adc_read_vbat_mv(&g_npm1300, &mv);
	chg_st = npm1300_charger_get_status(&g_npm1300, &st);
	if (adc_st == NPM1300_OK) {
		s_batt_mv = mv;
		s_batt_pct = npm1300_board_vbat_to_pct(mv);
		s_batt_valid = true;
	}
	if (chg_st == NPM1300_OK) {
		s_chg_status = st;
	}
}

static void npm1300_board_charge_sync(void)
{
	bool want;
	bool vbus;
	bool charging;
	bool full;

	k_mutex_lock(&s_npm_lock, K_FOREVER);
	vbus = npm1300_board_vbus_present();
	s_vbus_cached = vbus;
	want = vbus && !s_charge_inhibit;
	if (want != s_charge_applied) {
		if (npm1300_board_charge_apply(want) != 0) {
			printk("npm1300 chg sync fail want=%d\n", (int)want);
		}
	}
	npm1300_board_batt_sample_locked();
	charging = ((s_chg_status & NPM1300_CHG_ACTIVE) != 0U);
	full = ((s_chg_status & NPM1300_CHG_COMPLETED) != 0U);
	k_mutex_unlock(&s_npm_lock);

	npm1300_board_led_update(vbus, charging, full);

	if (s_batt_valid) {
		static uint8_t s_log_div;

		if (++s_log_div >= 10U) { /* ~10s */
			s_log_div = 0U;
			printk("npm1300: batt %umV %u%% vbus=%d chg=0x%02x%s%s\n",
			       (unsigned)s_batt_mv, (unsigned)s_batt_pct, (int)vbus,
			       (unsigned)s_chg_status, charging ? " charging" : "",
			       full ? " full" : "");
		}
	}
}

bool npm1300_board_vbus_get(void)
{
	bool vbus;

	k_mutex_lock(&s_npm_lock, K_FOREVER);
	vbus = s_vbus_cached;
	k_mutex_unlock(&s_npm_lock);
	return vbus;
}

void npm1300_board_led_suppress_set(bool suppress)
{
	s_led_suppress = suppress;
	if (suppress) {
		(void)k_work_cancel_delayable(&s_led_pulse_work);
	}
}

bool npm1300_board_batt_get(uint16_t *mv_out, uint8_t *pct_out, bool *charging_out,
			    bool *full_out)
{
	bool ok;

	k_mutex_lock(&s_npm_lock, K_FOREVER);
	ok = s_batt_valid;
	if (mv_out != NULL) {
		*mv_out = s_batt_mv;
	}
	if (pct_out != NULL) {
		*pct_out = s_batt_pct;
	}
	if (charging_out != NULL) {
		*charging_out = ((s_chg_status & NPM1300_CHG_ACTIVE) != 0U);
	}
	if (full_out != NULL) {
		*full_out = ((s_chg_status & NPM1300_CHG_COMPLETED) != 0U);
	}
	k_mutex_unlock(&s_npm_lock);
	return ok;
}

static int npm1300_board_gpio_init(void)
{
	if (!gpio_is_ready_dt(&s_epd_pwr) || !gpio_is_ready_dt(&s_touch_ldo)) {
		printk("npm1300 gpio not ready\n");
		return -1;
	}
	if (gpio_pin_configure_dt(&s_epd_pwr, GPIO_OUTPUT_INACTIVE) != 0) {
		return -2;
	}
	if (gpio_pin_configure_dt(&s_touch_ldo, GPIO_OUTPUT_INACTIVE) != 0) {
		return -3;
	}
	s_gpio_ready = true;
	return 0;
}

/** 触摸 LDO1；墨水屏 LDSW2 必须 3.3V（驱动上限） */
#define NPM1300_BOARD_TOUCH_MV 3000U
#define NPM1300_BOARD_EPD_MV   3300U

/**
 * 配 LDO1=触摸、LDSW2=EPD 3.3V（LDO 模式）。
 * TEMP：两路常开；并回读 VOUTSEL/LDOSEL，避免 LoadSW 透传成 ~3.0V。
 */
static int npm1300_board_rails_config(void)
{
	npm1300_status_t st;
	uint8_t ldosel = 0U;
	uint8_t voutsel = 0U;
	uint16_t epd_mv;

	st = npm1300_ldsw_set_mode(&g_npm1300, NPM1300_LDSW1, NPM1300_LDSW_MODE_LDO);
	if (st != NPM1300_OK) {
		return -1;
	}
	st = npm1300_ldsw_set_vout_mv(&g_npm1300, NPM1300_LDSW1, NPM1300_BOARD_TOUCH_MV);
	if (st != NPM1300_OK) {
		return -2;
	}

	/* EPD：先设 LDO 模式再写 3.3V，使能后再写一次防被默认覆盖 */
	st = npm1300_ldsw_set_mode(&g_npm1300, NPM1300_LDSW2, NPM1300_LDSW_MODE_LDO);
	if (st != NPM1300_OK) {
		return -3;
	}
	st = npm1300_ldsw_set_vout_mv(&g_npm1300, NPM1300_LDSW2, NPM1300_BOARD_EPD_MV);
	if (st != NPM1300_OK) {
		return -4;
	}

	s_rails_cfg_ok = true;
	npm1300_board_touch_power_set(true);
	npm1300_board_epd_power_set(true);

	(void)npm1300_ldsw_set_mode(&g_npm1300, NPM1300_LDSW2, NPM1300_LDSW_MODE_LDO);
	(void)npm1300_ldsw_set_vout_mv(&g_npm1300, NPM1300_LDSW2, NPM1300_BOARD_EPD_MV);

	(void)npm1300_reg_read(&g_npm1300, 0x0809u, &ldosel); /* LDSW2LDOSEL */
	(void)npm1300_reg_read(&g_npm1300, 0x080Du, &voutsel); /* LDSW2VOUTSEL */
	epd_mv = (uint16_t)(1000U + ((uint16_t)voutsel * 100U));
	if (((ldosel & 0x01u) == 0U) || (epd_mv != NPM1300_BOARD_EPD_MV)) {
		printk("npm1300: EPD rail warn LDOSEL=0x%02x => %umV\n", ldosel,
		       (unsigned)epd_mv);
	}

	return 0;
}

/** LDSWSTATUS：bit2=LoadSW2，bit3=LDO2（EPD 必须 LDO） */
#define NPM1300_LDSW_STAT_LDO2  0x08u
#define NPM1300_LDSW_STAT_LSW2  0x04u

void npm1300_board_epd_power_set(bool on)
{
	npm1300_status_t st;
	uint8_t ldsw = 0U;
	uint8_t ldosel = 0U;
	uint8_t voutsel = 0U;

	k_mutex_lock(&s_npm_lock, K_FOREVER);
	if (s_gpio_ready) {
		gpio_pin_set_dt(&s_epd_pwr, on ? 1 : 0);
	}
	if (!(s_rails_cfg_ok && g_npm1300.initialized)) {
		k_mutex_unlock(&s_npm_lock);
		return;
	}

	if (!on) {
		st = npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW2, false);
		if (st != NPM1300_OK) {
			printk("npm1300: LDSW2 disable FAIL st=%d\n", (int)st);
		}
		k_mutex_unlock(&s_npm_lock);
		return;
	}

	/*
	 * PS：改 LDOSEL/VOUTSEL 须在 LDSW 关闭时。
	 * 每次上电都重配，避免上次刷屏 OCP 掉电后模式回 LoadSW（~BUCK 3.0V）。
	 */
	(void)npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW2, false);
	k_mutex_unlock(&s_npm_lock);
	k_msleep(5);
	k_mutex_lock(&s_npm_lock, K_FOREVER);

	st = npm1300_ldsw_set_mode(&g_npm1300, NPM1300_LDSW2, NPM1300_LDSW_MODE_LDO);
	if (st == NPM1300_OK) {
		st = npm1300_ldsw_set_vout_mv(&g_npm1300, NPM1300_LDSW2, NPM1300_BOARD_EPD_MV);
	}
	if (st == NPM1300_OK) {
		st = npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW2, true);
	}
	if (st != NPM1300_OK) {
		printk("npm1300: LDSW2 bringup FAIL st=%d — retry\n", (int)st);
		(void)npm1300_ldsw_set_mode(&g_npm1300, NPM1300_LDSW2, NPM1300_LDSW_MODE_LDO);
		(void)npm1300_ldsw_set_vout_mv(&g_npm1300, NPM1300_LDSW2, NPM1300_BOARD_EPD_MV);
		st = npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW2, true);
	}

	(void)npm1300_reg_read(&g_npm1300, 0x0809u, &ldosel);
	(void)npm1300_reg_read(&g_npm1300, 0x080Du, &voutsel);
	(void)npm1300_ldsw_get_status(&g_npm1300, NPM1300_LDSW2, &ldsw);
	k_mutex_unlock(&s_npm_lock);

	if ((st != NPM1300_OK) || ((ldsw & NPM1300_LDSW_STAT_LDO2) == 0U) ||
	    ((ldsw & NPM1300_LDSW_STAT_LSW2) != 0U) || ((ldosel & 0x01u) == 0U)) {
		printk("npm1300: EPD rail warn st=%d LDOSEL=0x%02x VOUT=0x%02x ldsw=0x%02x\n",
		       (int)st, ldosel, voutsel, ldsw);
	}
}

bool npm1300_board_epd_rail_ok(void)
{
	uint8_t ldsw = 0U;

	if (!(s_rails_cfg_ok && g_npm1300.initialized)) {
		return false;
	}
	k_mutex_lock(&s_npm_lock, K_FOREVER);
	(void)npm1300_ldsw_get_status(&g_npm1300, NPM1300_LDSW2, &ldsw);
	k_mutex_unlock(&s_npm_lock);
	return (ldsw & NPM1300_LDSW_STAT_LDO2) != 0U;
}

/** LDSWSTATUS：bit0=LoadSW1，bit1=LDO1（触摸） */
#define NPM1300_LDSW_STAT_LDO1 0x02u

void npm1300_board_touch_power_set(bool on)
{
	npm1300_status_t st = NPM1300_OK;
	uint8_t ldsw = 0U;

	k_mutex_lock(&s_npm_lock, K_FOREVER);
	if (s_gpio_ready) {
		gpio_pin_set_dt(&s_touch_ldo, on ? 1 : 0);
	}
	if (!(s_rails_cfg_ok && g_npm1300.initialized)) {
		k_mutex_unlock(&s_npm_lock);
		return;
	}

	if (!on) {
		(void)npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW1, false);
		k_mutex_unlock(&s_npm_lock);
		return;
	}

	/* 与 EPD 同套路：关 → 写 LDO 模式/电压 → 开（防残留 LoadSW） */
	(void)npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW1, false);
	k_mutex_unlock(&s_npm_lock);
	k_msleep(5);
	k_mutex_lock(&s_npm_lock, K_FOREVER);

	st = npm1300_ldsw_set_mode(&g_npm1300, NPM1300_LDSW1, NPM1300_LDSW_MODE_LDO);
	if (st == NPM1300_OK) {
		st = npm1300_ldsw_set_vout_mv(&g_npm1300, NPM1300_LDSW1, NPM1300_BOARD_TOUCH_MV);
	}
	if (st == NPM1300_OK) {
		st = npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW1, true);
	}
	(void)npm1300_ldsw_get_status(&g_npm1300, NPM1300_LDSW1, &ldsw);
	k_mutex_unlock(&s_npm_lock);

	if ((st != NPM1300_OK) || ((ldsw & NPM1300_LDSW_STAT_LDO1) == 0U)) {
		printk("npm1300: touch rail warn st=%d ldsw=0x%02x\n", (int)st, ldsw);
	}
}

void npm1300_board_touch_rail_log(const char *tag)
{
	uint8_t ldsw = 0U;
	uint8_t ldosel = 0U;
	uint8_t voutsel = 0U;
	int en_gpio = -1;

	if (s_gpio_ready) {
		en_gpio = gpio_pin_get_dt(&s_touch_ldo);
	}

	if (s_rails_cfg_ok && g_npm1300.initialized) {
		k_mutex_lock(&s_npm_lock, K_FOREVER);
		(void)npm1300_ldsw_get_status(&g_npm1300, NPM1300_LDSW1, &ldsw);
		(void)npm1300_reg_read(&g_npm1300, 0x0808u, &ldosel); /* LDSW1LDOSEL */
		(void)npm1300_reg_read(&g_npm1300, 0x080Cu, &voutsel); /* LDSW1VOUTSEL */
		k_mutex_unlock(&s_npm_lock);
	}

	printk("npm1300: touch rail [%s] EN_GPIO=%d ldsw=0x%02x LDO1=%d LDOSEL=0x%02x VOUT=0x%02x (~%umV)\n",
	       (tag != NULL) ? tag : "?", en_gpio, ldsw,
	       (ldsw & NPM1300_LDSW_STAT_LDO1) ? 1 : 0, ldosel, voutsel,
	       (unsigned)(1000U + ((uint16_t)voutsel * 100U)));
}

void npm1300_board_charge_inhibit_set(bool inhibit)
{
	s_charge_inhibit = inhibit;
	if (g_npm1300.initialized) {
		npm1300_board_charge_sync();
	}
}

void npm1300_board_poll_charge(void)
{
	if (!g_npm1300.initialized) {
		return;
	}
	npm1300_board_charge_sync();
}

static void npm1300_board_charge_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	npm1300_board_poll_charge();
	(void)k_work_reschedule(&s_charge_work, K_SECONDS(1));
}

static void npm1300_board_workers_start(void)
{
	/* 开机场景 CYCLE_ALWAYS，否则会覆盖充电 LED */
	led_scene_cancel(LED_SCENE_ID_BOOTUP);
	k_work_init_delayable(&s_charge_work, npm1300_board_charge_work_fn);
	k_work_init_delayable(&s_led_pulse_work, npm1300_board_led_pulse_off_fn);
	(void)k_work_schedule(&s_charge_work, K_SECONDS(1));
}

int npm1300_board_enter_ship(void)
{
	npm1300_status_t st;

	if (!g_npm1300.initialized) {
		return -1;
	}

	s_charge_inhibit = true;
	k_mutex_lock(&s_npm_lock, K_FOREVER);
	(void)npm1300_board_charge_apply(false);
	k_mutex_unlock(&s_npm_lock);
	npm1300_board_epd_power_set(false);

	k_mutex_lock(&s_npm_lock, K_FOREVER);
	st = npm1300_ship_enter(&g_npm1300);
	k_mutex_unlock(&s_npm_lock);
	if (st != NPM1300_OK) {
		printk("npm1300 ship=%d\n", (int)st);
		return -2;
	}
	/* 之后 I2C 未定义；不再访问 g_npm1300 */
	return 0;
}

int npm1300_board_bringup(void)
{
	const struct device *i2c = npm1300_board_i2c();
	npm1300_config_t cfg = {
		.write = npm1300_board_write,
		.write_read = npm1300_board_write_read,
		.delay_ms = npm1300_board_delay_ms,
		.i2c_addr = NPM1300_ADDR_7BIT_DEFAULT,
	};
	npm1300_status_t st;

	s_charge_inhibit = false;
	s_charge_applied = false;
	s_rails_cfg_ok = false;

	if (!device_is_ready(i2c)) {
		printk("npm1300 i2c fail\n");
		return -1;
	}

	st = npm1300_init(&g_npm1300, &cfg);
	if (st != NPM1300_OK) {
		printk("npm1300 init=%d\n", (int)st);
		return -2;
	}

	st = npm1300_buck_set_vout_mv(&g_npm1300, NPM1300_BUCK1, 3000U);
	if (st != NPM1300_OK) {
		printk("npm1300 buck1=%d\n", (int)st);
		return -3;
	}

	if (npm1300_board_gpio_init() != 0) {
		printk("npm1300 gpio init fail\n");
		return -4;
	}

	if (npm1300_board_rails_config() != 0) {
		printk("npm1300 rails cfg fail\n");
		return -5;
	}

	npm1300_board_charge_sync();
	npm1300_board_workers_start();
	return 0;
}
