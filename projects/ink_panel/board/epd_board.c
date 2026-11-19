/**
 * @file epd_board.c
 * @brief ink_panel EVT：GPIO bit-bang → cbb/epd（WS 4.2" B V2 BWR）
 *
 * 板级仅 DT 脚 + nPM1300 供电；BWR demo 走 cbb epd_display_demo_bwr_fill（与 eink_tag 共用）。
 * SPIM 在本板 Bus Fault，故只用 bit-bang。
 */

#include "epd_board.h"

#include "npm1300_board.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include "epd_display.h"
#include "epd_panel.h"

#define ZUSER DT_PATH(zephyr_user)

#define EPD_PLANE_CAPACITY 15000U /* 400×300/8 */
#define EPD_PWR_SETTLE_MS  50U /* 等 LDSW2 3.3V 起来 */
#define EPD_BUSY_PROBE_MS  2000U
/** 对齐 eink_tag：BUSY 不可用时全刷后再等多一截 */
#define EPD_FRAME_SETTLE_MS 10000U

static const struct gpio_dt_spec s_sck = GPIO_DT_SPEC_GET(ZUSER, epd_sck_gpios);
static const struct gpio_dt_spec s_mosi = GPIO_DT_SPEC_GET(ZUSER, epd_mosi_gpios);
static const struct gpio_dt_spec s_cs = GPIO_DT_SPEC_GET(ZUSER, epd_cs_gpios);
static const struct gpio_dt_spec s_dc = GPIO_DT_SPEC_GET(ZUSER, epd_dc_gpios);
static const struct gpio_dt_spec s_rst = GPIO_DT_SPEC_GET(ZUSER, epd_rst_gpios);
static const struct gpio_dt_spec s_busy = GPIO_DT_SPEC_GET(ZUSER, epd_busy_gpios);

static uint8_t s_bw[EPD_PLANE_CAPACITY];
static uint8_t s_red[EPD_PLANE_CAPACITY];
static epd_display_t s_disp;
static bool s_ready;
static bool s_busy_usable;
static atomic_t s_refreshing;

K_THREAD_STACK_DEFINE(s_epd_stack, 4096);
static struct k_thread s_epd_thread;
static bool s_smoke_started;

static void epd_bb_delay(void)
{
	k_busy_wait(2);
}

static void epd_bb_tx_byte(uint8_t byte)
{
	for (int8_t bit = 7; bit >= 0; bit--) {
		(void)gpio_pin_set_raw(s_mosi.port, s_mosi.pin, (byte >> bit) & 0x1);
		epd_bb_delay();
		(void)gpio_pin_set_raw(s_sck.port, s_sck.pin, 1);
		epd_bb_delay();
		(void)gpio_pin_set_raw(s_sck.port, s_sck.pin, 0);
		epd_bb_delay();
	}
}

static void epd_spi_tx(const uint8_t *data, uint16_t len)
{
	if ((data == NULL) || (len == 0U)) {
		return;
	}
	for (uint16_t i = 0; i < len; i++) {
		epd_bb_tx_byte(data[i]);
	}
}

static void epd_cs(int high)
{
	(void)gpio_pin_set_raw(s_cs.port, s_cs.pin, high ? 1 : 0);
}

static void epd_dc(int high)
{
	(void)gpio_pin_set_raw(s_dc.port, s_dc.pin, high ? 1 : 0);
}

static void epd_rst(int high)
{
	(void)gpio_pin_set_raw(s_rst.port, s_rst.pin, high ? 1 : 0);
}

static int epd_busy(void)
{
	return gpio_pin_get_raw(s_busy.port, s_busy.pin);
}

static void epd_delay_ms(uint32_t ms)
{
	k_msleep(ms);
}

static void epd_power_on(void)
{
	npm1300_board_epd_power_set(true);
	k_msleep(EPD_PWR_SETTLE_MS);
}

static void epd_power_off(void)
{
	/* 冒烟阶段保持常供 */
}

static status_t epd_gpio_init(void)
{
	/* 对齐 eink_tag：INACTIVE/ACTIVE + BUSY 上拉（勿对 ACTIVE_LOW CS 用 OUTPUT_HIGH） */
	if (!gpio_is_ready_dt(&s_sck) || !gpio_is_ready_dt(&s_mosi) ||
	    !gpio_is_ready_dt(&s_cs) || !gpio_is_ready_dt(&s_dc) ||
	    !gpio_is_ready_dt(&s_rst) || !gpio_is_ready_dt(&s_busy)) {
		printk("epd gpio not ready\n");
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_sck, GPIO_OUTPUT_INACTIVE) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_mosi, GPIO_OUTPUT_INACTIVE) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_cs, GPIO_OUTPUT_INACTIVE) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_dc, GPIO_OUTPUT_INACTIVE) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_rst, GPIO_OUTPUT_ACTIVE) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_busy, GPIO_INPUT | GPIO_PULL_UP) != 0) {
		return STATUS_FAIL;
	}
	epd_cs(1);
	epd_rst(1);
	return STATUS_OK;
}

/**
 * 等 BUSY 回到空闲（active-high → 读到 0）。
 * @return true=空闲；false=超时仍高（断线/未供电时常被 R18 上拉成高）
 */
static bool epd_wait_busy_idle(uint32_t timeout_ms, uint32_t *waited_out)
{
	uint32_t waited = 0U;

	while ((epd_busy() != 0) && (waited < timeout_ms)) {
		k_msleep(50);
		waited += 50U;
	}
	if (waited_out != NULL) {
		*waited_out = waited;
	}
	return epd_busy() == 0;
}

static bool epd_busy_probe(void)
{
	uint32_t waited = 0U;
	uint32_t sample = 0U;
	bool saw_high = false;

	printk("epd busy raw=%d before rst\n", epd_busy());

	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(5);

	/* 复位后短窗内应能看到 BUSY 变高；一直为 0 = 线死/无板上拉 → 不可用 */
	while (sample < 500U) {
		if (epd_busy() != 0) {
			saw_high = true;
			break;
		}
		k_msleep(5);
		sample += 5U;
	}
	printk("epd busy after rst saw_high=%d raw=%d\n", saw_high ? 1 : 0, epd_busy());

	if (!epd_wait_busy_idle(EPD_BUSY_PROBE_MS, &waited)) {
		printk("epd busy stuck HIGH %ums — use fixed wait (eink_tag path)\n",
		       (unsigned)waited);
		return false;
	}

	if (!saw_high) {
		/*
		 * 与 eink_tag 差异补丁：一直空闲(LOW)却从未变高，
		 * 若仍挂 read_busy，全刷 wait_idle 会 0ms 返回 → 屏不更新。
		 * 强制走库内 ~16s 固定延时。
		 */
		printk("epd busy never HIGH — disable read_busy, use fixed ~16s wait\n");
		return false;
	}

	printk("epd busy idle ok waited=%ums (usable)\n", (unsigned)waited);
	return true;
}

/** SSD168x SWRESET（写命令 0x12）；无 MISO，只能靠 BUSY 看是否吃进命令 */
static void epd_bb_write_cmd(uint8_t cmd)
{
	epd_dc(0);
	epd_cs(0);
	epd_bb_tx_byte(cmd);
	epd_cs(1);
}

status_t epd_board_hw_scan(void)
{
	int busy_pre_pwr;
	int busy_post_pwr;
	int busy_post_rst;
	int busy_saw_high;
	uint32_t waited = 0U;
	bool rst_ok;
	bool swreset_ok;
	uint8_t pat[4] = {0xA5U, 0x5AU, 0xF0U, 0x0FU};

	printk("==== EPD HW SCAN (bitbang, no MISO / no SPI addr scan) ====\n");
	printk("epd pins: SCK=P1.08 MOSI=P1.09 CS=P1.10 DC=P1.05 RST=P1.06 BUSY=P1.07\n");
	printk("epd note: BUSY R18 pull-up to VDD_SoC; absent panel stays HIGH\n");

	if (epd_gpio_init() != STATUS_OK) {
		printk("epd SCAN: GPIO FAIL\n");
		printk("==== EPD HW SCAN RESULT: ABSENT (gpio) ====\n");
		return STATUS_FAIL;
	}

	busy_pre_pwr = epd_busy();
	printk("epd SCAN: BUSY=%d before EPD power\n", busy_pre_pwr);

	epd_power_on();
	busy_post_pwr = epd_busy();
	printk("epd SCAN: BUSY=%d after EPD power (+%ums settle)\n", busy_post_pwr,
	       (unsigned)EPD_PWR_SETTLE_MS);

	/* 示波/万用表可看：SCK/MOSI/CS 翻转，确认走线 */
	printk("epd SCAN: toggle SCK/MOSI/CS 3x (scope optional)\n");
	for (int i = 0; i < 3; i++) {
		(void)gpio_pin_set_raw(s_sck.port, s_sck.pin, 0);
		(void)gpio_pin_set_raw(s_mosi.port, s_mosi.pin, 0);
		epd_cs(0);
		k_msleep(50);
		(void)gpio_pin_set_raw(s_sck.port, s_sck.pin, 1);
		(void)gpio_pin_set_raw(s_mosi.port, s_mosi.pin, 1);
		epd_cs(1);
		k_msleep(50);
	}
	(void)gpio_pin_set_raw(s_sck.port, s_sck.pin, 0);
	(void)gpio_pin_set_raw(s_mosi.port, s_mosi.pin, 0);
	epd_cs(1);

	epd_cs(0);
	epd_spi_tx(pat, sizeof(pat));
	epd_cs(1);
	printk("epd SCAN: bitbang burst 0xA5 5A F0 0F done\n");

	/* --- 步骤1：硬件复位，看 BUSY 是否被面板拉回空闲(LOW) --- */
	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(5);
	busy_saw_high = 0;
	{
		uint32_t sample = 0U;

		while (sample < 200U) {
			if (epd_busy() != 0) {
				busy_saw_high = 1;
				break;
			}
			k_msleep(5);
			sample += 5U;
		}
	}
	k_msleep(15);
	busy_post_rst = epd_busy();
	printk("epd SCAN: after RST busy_saw_high=%d busy_now=%d\n", busy_saw_high,
	       busy_post_rst);

	rst_ok = epd_wait_busy_idle(EPD_BUSY_PROBE_MS, &waited);
	printk("epd SCAN: RST->idle %s waited=%ums busy=%d\n", rst_ok ? "OK" : "TIMEOUT",
	       (unsigned)waited, epd_busy());

	/* --- 步骤2：SWRESET 0x12，再等空闲（面板在才应响应） --- */
	epd_bb_write_cmd(0x12U);
	k_msleep(10);
	swreset_ok = epd_wait_busy_idle(EPD_BUSY_PROBE_MS, &waited);
	printk("epd SCAN: SWRESET(0x12)->idle %s waited=%ums busy=%d\n",
	       swreset_ok ? "OK" : "TIMEOUT", (unsigned)waited, epd_busy());

	/*
	 * 判定（对齐 eink_tag 经验）：
	 * - 无面板 + 上拉 → BUSY 常高 → rst/swreset 超时 → ABSENT
	 * - 有面板 → 应见过 HIGH，再回到 LOW
	 * - 一直 LOW 从未 HIGH → 线异常；仍允许 bringup 走固定延时，故返回 OK 但打 WARN
	 */
	if (busy_saw_high && (rst_ok || swreset_ok)) {
		printk("==== EPD HW SCAN RESULT: PRESENT (BUSY toggled) ====\n");
		return STATUS_OK;
	}
	if ((rst_ok || swreset_ok) && !busy_saw_high) {
		printk("epd SCAN: WARN always-LOW (no HIGH pulse) — continue with fixed wait\n");
		printk("==== EPD HW SCAN RESULT: PRESENT? (busy pin suspect) ====\n");
		return STATUS_OK;
	}

	printk("epd SCAN: BUSY never idle — check FPC/power/LS2/EPD_PWR_EN/wiring\n");
	printk("==== EPD HW SCAN RESULT: ABSENT (no BUSY response) ====\n");
	return STATUS_FAIL;
}

status_t epd_board_bringup(void)
{
	epd_display_config_t cfg;
	epd_display_status_t st;
	int64_t t0;

	s_ready = false;
	printk("epd bringup bitbang...\n");

	if (epd_gpio_init() != STATUS_OK) {
		printk("epd gpio init fail\n");
		return STATUS_FAIL;
	}

	epd_power_on();
	printk("epd power on, probe busy...\n");
	s_busy_usable = epd_busy_probe();

	memset(&cfg, 0, sizeof(cfg));
	cfg.epd.spi_tx = epd_spi_tx;
	cfg.epd.set_cs = epd_cs;
	cfg.epd.set_dc = epd_dc;
	cfg.epd.set_rst = epd_rst;
	cfg.epd.read_busy = s_busy_usable ? epd_busy : NULL;
	cfg.epd.delay_ms = epd_delay_ms;
	cfg.epd.busy_active_high = true;
	cfg.epd.bw_invert = false;
	cfg.epd.panel = &epd_panel_ws_4in2b_v2;
	cfg.bw = s_bw;
	cfg.red = s_red;
	cfg.plane_capacity = EPD_PLANE_CAPACITY;
	cfg.power_on = epd_power_on;
	cfg.power_off = epd_power_off;
	cfg.hw_clear_on_init = false;

	printk("epd display_init busy=%s...\n", s_busy_usable ? "pin" : "fixed");
	t0 = k_uptime_get();
	st = epd_display_init(&s_disp, &cfg);
	printk("epd display_init st=%d dt=%lldms\n", (int)st,
	       (long long)(k_uptime_get() - t0));
	if (st != EPD_DISPLAY_OK) {
		return STATUS_FAIL;
	}

	s_ready = true;
	printk("epd ok panel=%s %ux%u path=bitbang\n", epd_panel_ws_4in2b_v2.name,
	       (unsigned)epd_display_width(&s_disp),
	       (unsigned)epd_display_height(&s_disp));
	return STATUS_OK;
}

bool epd_board_is_ready(void)
{
	return s_ready && epd_display_is_ready(&s_disp);
}

bool epd_board_is_busy(void)
{
	return atomic_get(&s_refreshing) != 0;
}

static status_t epd_refresh_step(const char *name, epd_display_status_t (*fn)(epd_display_t *))
{
	int64_t t0;
	int64_t dt;
	epd_display_status_t st;

	t0 = k_uptime_get();
	st = fn(&s_disp);
	if (!s_busy_usable) {
		k_msleep(EPD_FRAME_SETTLE_MS);
	}
	dt = k_uptime_get() - t0;

	printk("epd: %s st=%d(%s) dt=%lldms busy_ok=%d\n", name, (int)st,
	       (st == EPD_DISPLAY_OK)               ? "ok"
	       : (st == EPD_DISPLAY_ERROR_BUSY)     ? "BUSY_TIMEOUT"
	       : (st == EPD_DISPLAY_ERROR_PARAM)    ? "PARAM"
	       : (st == EPD_DISPLAY_ERROR_NOT_INIT) ? "NOT_INIT"
						    : "ERR",
	       (long long)dt, s_busy_usable ? 1 : 0);

	return (st == EPD_DISPLAY_OK) ? STATUS_OK : STATUS_FAIL;
}

status_t epd_board_test_run(void)
{
	status_t st;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}

	if (atomic_get(&s_refreshing) == 0) {
		if (atomic_cas(&s_refreshing, 0, 1) == false) {
			printk("epd: demo skipped (refresh busy)\n");
			return STATUS_INVALID_STATE;
		}
	}

	printk("epd: draw BWR demo (chars + black/red/white), one refresh\n");
	epd_display_demo_bwr_fill(&s_disp, "ink_panel", "4.2in BWR demo",
				  epd_panel_ws_4in2b_v2.name);

	st = epd_refresh_step("demo", epd_display_refresh);
	atomic_set(&s_refreshing, 0);
	if (st != STATUS_OK) {
		return st;
	}

	printk("epd: demo done — should show text + 3 colors\n");
	return STATUS_OK;
}

static void epd_smoke_thread(void *a, void *b, void *c)
{
	status_t st;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	printk("epd: worker start\n");

	if (epd_board_hw_scan() != STATUS_OK) {
		printk("epd: HW ABSENT — skip demo\n");
		return;
	}

	st = epd_board_bringup();
	if (st != STATUS_OK) {
		printk("epd: bringup failed: %d\n", (int)st);
		return;
	}

	printk("epd: ready board=%ux%u panel=%s — boot demo\n",
	       (unsigned)epd_display_width(&s_disp), (unsigned)epd_display_height(&s_disp),
	       epd_panel_ws_4in2b_v2.name);

	printk("epd: boot demo start\n");
	st = epd_board_test_run();
	if (st != STATUS_OK) {
		printk("epd: boot demo failed: %d\n", (int)st);
	} else {
		printk("epd: boot demo done\n");
	}
}

void epd_board_smoke_start(void)
{
	if (s_smoke_started) {
		return;
	}
	s_smoke_started = true;
	atomic_set(&s_refreshing, 0);
	(void)k_thread_create(&s_epd_thread, s_epd_stack, K_THREAD_STACK_SIZEOF(s_epd_stack),
			      epd_smoke_thread, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	k_thread_name_set(&s_epd_thread, "epd");
	printk("epd: async worker created\n");
}
