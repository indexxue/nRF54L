/**
 * @file epd_board.c
 * @brief ink_panel EVT：GPIO bit-bang → cbb/epd（WS 4.2" B V2 BWR）
 *
 * spi20 首次传输会在 nRF54 SPIM SCK 预置电平路径上 Bus Fault（BFAR=0x8）；
 * 冒烟阶段改 bit-bang。控制台仍用 uart00。BUSY 卡死则改固定延时。
 */

#include "epd_board.h"

#include "npm1300_board.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "epd_display.h"
#include "epd_panel.h"

#define ZUSER DT_PATH(zephyr_user)

#define EPD_PLANE_CAPACITY 15000U /* 400×300/8 */
#define EPD_PWR_SETTLE_MS  20U
#define EPD_BUSY_PROBE_MS  2000U

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
	if (!gpio_is_ready_dt(&s_sck) || !gpio_is_ready_dt(&s_mosi) ||
	    !gpio_is_ready_dt(&s_cs) || !gpio_is_ready_dt(&s_dc) ||
	    !gpio_is_ready_dt(&s_rst) || !gpio_is_ready_dt(&s_busy)) {
		printk("epd gpio not ready\n");
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_sck, GPIO_OUTPUT_LOW) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_mosi, GPIO_OUTPUT_LOW) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_cs, GPIO_OUTPUT_HIGH) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_dc, GPIO_OUTPUT_LOW) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_rst, GPIO_OUTPUT_HIGH) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_busy, GPIO_INPUT) != 0) {
		return STATUS_FAIL;
	}
	epd_cs(1);
	epd_rst(1);
	return STATUS_OK;
}

static bool epd_busy_probe(void)
{
	uint32_t waited = 0U;

	printk("epd busy raw=%d before rst\n", epd_busy());

	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(20);
	printk("epd busy raw=%d after rst\n", epd_busy());

	while ((epd_busy() != 0) && (waited < EPD_BUSY_PROBE_MS)) {
		k_msleep(50);
		waited += 50U;
	}

	if (epd_busy() != 0) {
		printk("epd busy stuck HIGH %ums — use fixed wait\n", (unsigned)waited);
		return false;
	}

	printk("epd busy idle ok waited=%ums\n", (unsigned)waited);
	return true;
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
	return s_ready;
}

static void epd_smoke_thread(void *a, void *b, void *c)
{
	uint16_t w;
	uint16_t h;
	uint16_t x_max;
	uint16_t y_max;
	uint16_t col_w;
	const uint16_t title_h = 40U;
	uint16_t band_y0;
	uint16_t band_y1;
	epd_display_status_t st;
	int64_t t0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (epd_board_bringup() != STATUS_OK) {
		printk("epd smoke: bringup fail\n");
		return;
	}

	w = epd_display_width(&s_disp);
	h = epd_display_height(&s_disp);
	x_max = (uint16_t)(w - 1U);
	y_max = (uint16_t)(h - 1U);
	col_w = (uint16_t)(w / 3U);
	band_y0 = 72U;
	band_y1 = (uint16_t)(h - 56U);

	printk("epd smoke: draw BWR demo...\n");
	epd_display_clear_buffer(&s_disp, EPD_COLOR_WHITE);

	epd_display_fill_rect(&s_disp, 0U, 0U, x_max, (uint16_t)(title_h - 1U), EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, 6U, "ink_panel", EPD_FONT_16, EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 24U, "4.2in BWR smoke", EPD_FONT_12, EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 48U, "Hello nRF54L15!", EPD_FONT_16, EPD_COLOR_BLACK);

	epd_display_fill_rect(&s_disp, 0U, band_y0, (uint16_t)(col_w - 2U), band_y1, EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, (uint16_t)(band_y0 + 16U), "BLACK", EPD_FONT_12,
				EPD_COLOR_WHITE);

	epd_display_fill_rect(&s_disp, col_w, band_y0, (uint16_t)(col_w * 2U - 2U), band_y1,
			      EPD_COLOR_RED);
	epd_display_draw_string(&s_disp, (uint16_t)(col_w + 4U), (uint16_t)(band_y0 + 16U), "RED",
				EPD_FONT_12, EPD_COLOR_WHITE);

	epd_display_fill_rect(&s_disp, (uint16_t)(col_w * 2U), band_y0, x_max, band_y1,
			      EPD_COLOR_WHITE);
	epd_display_draw_rect(&s_disp, (uint16_t)(col_w * 2U), band_y0, x_max, band_y1,
			      EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, (uint16_t)(col_w * 2U + 2U), (uint16_t)(band_y0 + 16U),
				"WHITE", EPD_FONT_12, EPD_COLOR_BLACK);

	epd_display_draw_string(&s_disp, 4U, (uint16_t)(band_y1 + 8U), "BLACK / RED / WHITE",
				EPD_FONT_12, EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, (uint16_t)(band_y1 + 24U), "WS_4IN2B_V2", EPD_FONT_12,
				EPD_COLOR_RED);
	epd_display_draw_rect(&s_disp, 0U, 0U, x_max, y_max, EPD_COLOR_BLACK);

	t0 = k_uptime_get();
	st = epd_display_refresh(&s_disp);
	printk("epd smoke: refresh st=%d dt=%lldms\n", (int)st,
	       (long long)(k_uptime_get() - t0));
}

void epd_board_smoke_start(void)
{
	if (s_smoke_started) {
		return;
	}
	s_smoke_started = true;
	(void)k_thread_create(&s_epd_thread, s_epd_stack, K_THREAD_STACK_SIZEOF(s_epd_stack),
			      epd_smoke_thread, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(&s_epd_thread, "epd_smoke");
}
