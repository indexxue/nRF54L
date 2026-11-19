/**
 * @file npm1300_board.c
 * @brief ink_panel：i2c21 → nPM1300 板级适配与产品电源策略
 *
 * 轨门控以 I2C LDSW 为准（默认关，须 TASK 使能）。MCU EN 脚同步驱动，
 * 供日后配 LDSWnGPISEL；当前未映射时单独拉 GPIO 不会出电压。
 */

#include "npm1300_board.h"

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define ZUSER DT_PATH(zephyr_user)

/* 与 cbb / PS 一致，板级回读与 VBUS 判定 */
#define NPM1300_BOARD_REG_BUCK1NORMVOUT 0x0408u
#define NPM1300_BOARD_REG_BUCKSWCTRLSEL 0x040Fu
#define NPM1300_BOARD_REG_BUCK1VOUTST   0x0410u
#define NPM1300_BOARD_REG_VBUSINSTATUS0 0x0207u
#define NPM1300_BOARD_VBUS_DETECTED     0x01u /* VBUSINSTATUS bit0 */

#define NPM1300_BOARD_ICHG_MA  300u
#define NPM1300_BOARD_VTERM_MV 4200u

npm1300_t g_npm1300;

static const struct gpio_dt_spec s_epd_pwr =
	GPIO_DT_SPEC_GET(ZUSER, epd_pwr_gpios);
static const struct gpio_dt_spec s_touch_ldo =
	GPIO_DT_SPEC_GET(ZUSER, touch_ldo_gpios);

static bool s_charge_inhibit;
static bool s_charge_applied; /* 上次已成功使能充电 */
static bool s_gpio_ready;
static bool s_rails_cfg_ok;

static struct k_work_delayable s_charge_work;
static struct k_work_delayable s_status_work;

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

static void npm1300_board_charge_sync(void)
{
	const bool want = npm1300_board_vbus_present() && !s_charge_inhibit;

	if (want == s_charge_applied) {
		return;
	}
	if (npm1300_board_charge_apply(want) != 0) {
		printk("npm1300 chg sync fail want=%d\n", (int)want);
	}
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
	printk("npm1300 EPD rail: LDOSEL=0x%02x VOUTSEL=0x%02x => %umV (want %u)\n",
	       ldosel, voutsel, (unsigned)epd_mv, (unsigned)NPM1300_BOARD_EPD_MV);
	if ((ldosel & 0x01u) == 0U) {
		printk("npm1300 EPD WARN: not LDO mode (LoadSW may pass ~BUCK/VSYS)\n");
	}
	if (epd_mv != NPM1300_BOARD_EPD_MV) {
		printk("npm1300 EPD WARN: vout not 3.3V\n");
	}

	return 0;
}

void npm1300_board_epd_power_set(bool on)
{
	if (s_gpio_ready) {
		gpio_pin_set_dt(&s_epd_pwr, on ? 1 : 0);
	}
	if (s_rails_cfg_ok && g_npm1300.initialized) {
		(void)npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW2, on);
	}
}

void npm1300_board_touch_power_set(bool on)
{
	if (s_gpio_ready) {
		gpio_pin_set_dt(&s_touch_ldo, on ? 1 : 0);
	}
	if (s_rails_cfg_ok && g_npm1300.initialized) {
		(void)npm1300_ldsw_enable(&g_npm1300, NPM1300_LDSW1, on);
	}
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

static void npm1300_board_status_work_fn(struct k_work *work)
{
	ARG_UNUSED(work);
	npm1300_board_log_status();
	(void)k_work_reschedule(&s_status_work, K_SECONDS(5));
}

static void npm1300_board_workers_start(void)
{
	k_work_init_delayable(&s_charge_work, npm1300_board_charge_work_fn);
	k_work_init_delayable(&s_status_work, npm1300_board_status_work_fn);
	(void)k_work_schedule(&s_charge_work, K_SECONDS(1));
	(void)k_work_schedule(&s_status_work, K_SECONDS(5));
}

void npm1300_board_log_status(void)
{
	uint8_t buck = 0U;
	uint8_t chg = 0U;
	uint8_t ldsw = 0U;
	uint8_t vout = 0U;
	uint8_t sw = 0U;
	uint8_t vst = 0U;
	uint8_t vbus = 0U;

	if (!g_npm1300.initialized) {
		return;
	}

	(void)npm1300_buck_get_status(&g_npm1300, NPM1300_BUCK1, &buck);
	(void)npm1300_charger_get_status(&g_npm1300, &chg);
	(void)npm1300_ldsw_get_status(&g_npm1300, NPM1300_LDSW1, &ldsw);
	(void)npm1300_reg_read(&g_npm1300, NPM1300_BOARD_REG_BUCK1NORMVOUT, &vout);
	(void)npm1300_reg_read(&g_npm1300, NPM1300_BOARD_REG_BUCKSWCTRLSEL, &sw);
	(void)npm1300_reg_read(&g_npm1300, NPM1300_BOARD_REG_BUCK1VOUTST, &vst);
	(void)npm1300_reg_read(&g_npm1300, NPM1300_BOARD_REG_VBUSINSTATUS0, &vbus);

	printk("npm1300 vout=0x%02x sw=0x%02x vst=0x%02x buck=0x%02x chg=0x%02x ldsw=0x%02x vbus=0x%02x inh=%d applied=%d\n",
	       vout, sw, vst, buck, chg, ldsw, vbus, (int)s_charge_inhibit,
	       (int)s_charge_applied);
}

int npm1300_board_enter_ship(void)
{
	npm1300_status_t st;

	if (!g_npm1300.initialized) {
		return -1;
	}

	s_charge_inhibit = true;
	(void)npm1300_board_charge_apply(false);
	npm1300_board_epd_power_set(false);

	st = npm1300_ship_enter(&g_npm1300);
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

	printk("npm1300 ok buck1=3.0V touch=%uV epd=%uV(LDO) both=on chg=%s\n",
	       (unsigned)(NPM1300_BOARD_TOUCH_MV / 1000U),
	       (unsigned)(NPM1300_BOARD_EPD_MV / 1000U),
	       s_charge_applied ? "on" : "off");
	npm1300_board_log_status();
	return 0;
}
