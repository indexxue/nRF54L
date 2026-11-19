/**
 * @file epd_board.c
 * @brief zigbee_ed rev_b：GPIO bit-bang → cbb/epd（WS 4.2" B V2）+ 文本队列
 *
 * 文本入口：Zigbee DispText → epd_board_text_request()（K_NO_WAIT）；
 * 刷屏仅在本线程。
 */

#include "epd_board.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include "epd_display.h"
#include "epd_panel.h"

#define ZUSER DT_PATH(zephyr_user)

#define EPD_PLANE_CAPACITY 15000U
#define EPD_BUSY_PROBE_MS  2000U
#define EPD_FRAME_SETTLE_MS 10000U
#define EPD_SCK_PIN        1U /* P2.01 */
#define EPD_MOSI_PIN       2U /* P2.02 */
#define EPD_CS_PIN         5U /* P2.05 */

struct epd_text_job {
	char text[EPD_BOARD_TEXT_MAX + 1U];
};

static const struct gpio_dt_spec s_dc = GPIO_DT_SPEC_GET(ZUSER, epd_dc_gpios);
static const struct gpio_dt_spec s_rst = GPIO_DT_SPEC_GET(ZUSER, epd_rst_gpios);
static const struct gpio_dt_spec s_busy = GPIO_DT_SPEC_GET(ZUSER, epd_busy_gpios);
static const struct device *s_gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

static uint8_t s_bw[EPD_PLANE_CAPACITY];
static uint8_t s_red[EPD_PLANE_CAPACITY];
static epd_display_t s_disp;
static bool s_ready;
static bool s_busy_usable;
static bool s_started;

K_MSGQ_DEFINE(s_epd_q, sizeof(struct epd_text_job), 1, 4);
K_THREAD_STACK_DEFINE(s_epd_stack, 4096);
static struct k_thread s_epd_thread;

static void epd_bb_delay(void)
{
	k_busy_wait(2);
}

static void epd_bb_tx_byte(uint8_t byte)
{
	for (int8_t bit = 7; bit >= 0; bit--) {
		(void)gpio_pin_set_raw(s_gpio2, EPD_MOSI_PIN, (byte >> bit) & 0x1);
		epd_bb_delay();
		(void)gpio_pin_set_raw(s_gpio2, EPD_SCK_PIN, 1);
		epd_bb_delay();
		(void)gpio_pin_set_raw(s_gpio2, EPD_SCK_PIN, 0);
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
	(void)gpio_pin_set_raw(s_gpio2, EPD_CS_PIN, high ? 1 : 0);
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

static bool epd_busy_probe(void)
{
	int64_t t0 = k_uptime_get();

	while (epd_busy() != 0) {
		if ((k_uptime_get() - t0) > EPD_BUSY_PROBE_MS) {
			printk("epd: BUSY stuck — use fixed wait\n");
			return false;
		}
		k_msleep(10);
	}
	return true;
}

static status_t epd_gpio_init(void)
{
	if (!device_is_ready(s_gpio2) || !gpio_is_ready_dt(&s_dc) || !gpio_is_ready_dt(&s_rst) ||
	    !gpio_is_ready_dt(&s_busy)) {
		printk("epd: gpio not ready\n");
		return STATUS_FAIL;
	}

	if (gpio_pin_configure(s_gpio2, EPD_SCK_PIN, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure(s_gpio2, EPD_MOSI_PIN, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure(s_gpio2, EPD_CS_PIN, GPIO_OUTPUT_INACTIVE) != 0) {
		printk("epd: SCK/MOSI/CS configure fail\n");
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_dc, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_rst, GPIO_OUTPUT_ACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_busy, GPIO_INPUT | GPIO_PULL_UP) != 0) {
		printk("epd: DC/RST/BUSY configure fail\n");
		return STATUS_FAIL;
	}

	epd_cs(1);
	return STATUS_OK;
}

static status_t epd_board_bringup(void)
{
	epd_display_config_t cfg;
	epd_display_status_t st;
	int64_t t0;

	s_ready = false;
	if (epd_gpio_init() != STATUS_OK) {
		return STATUS_FAIL;
	}

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
	cfg.hw_clear_on_init = false;

	printk("epd: display_init busy=%s...\n", s_busy_usable ? "pin" : "fixed");
	t0 = k_uptime_get();
	st = epd_display_init(&s_disp, &cfg);
	printk("epd: display_init st=%d dt=%lldms\n", (int)st,
	       (long long)(k_uptime_get() - t0));
	if (st != EPD_DISPLAY_OK) {
		return STATUS_FAIL;
	}

	s_ready = true;
	printk("epd: ready panel=%s %ux%u path=bitbang\n", epd_panel_ws_4in2b_v2.name,
	       (unsigned)epd_display_width(&s_disp), (unsigned)epd_display_height(&s_disp));
	return STATUS_OK;
}

static void epd_board_paint_text(const char *text)
{
	uint16_t w = epd_display_width(&s_disp);
	uint16_t h = epd_display_height(&s_disp);
	uint16_t x_max = (uint16_t)(w - 1U);
	uint16_t y_max = (uint16_t)(h - 1U);
	epd_display_status_t st;
	int64_t t0;

	epd_display_clear_buffer(&s_disp, EPD_COLOR_WHITE);
	epd_display_fill_rect(&s_disp, 0U, 0U, x_max, 39U, EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, 6U, "zigbee_ed", EPD_FONT_16, EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 24U, "4.2in tuya", EPD_FONT_12, EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 56U, text, EPD_FONT_16, EPD_COLOR_BLACK);
	epd_display_draw_rect(&s_disp, 0U, 0U, x_max, y_max, EPD_COLOR_BLACK);

	t0 = k_uptime_get();
	st = epd_display_refresh(&s_disp);
	if (!s_busy_usable) {
		k_msleep(EPD_FRAME_SETTLE_MS);
	}
	printk("epd: text refresh st=%d dt=%lldms msg=\"%s\"\n", (int)st,
	       (long long)(k_uptime_get() - t0), text);
}

static void epd_worker(void *a, void *b, void *c)
{
	struct epd_text_job job;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	if (epd_board_bringup() != STATUS_OK) {
		printk("epd: bringup fail — text ignored\n");
		return;
	}

	epd_board_paint_text("wait tuya text DP");

	while (1) {
		if (k_msgq_get(&s_epd_q, &job, K_FOREVER) != 0) {
			continue;
		}
		epd_board_paint_text(job.text);
	}
}

void epd_board_start(void)
{
	if (s_started) {
		return;
	}
	s_started = true;
	(void)k_thread_create(&s_epd_thread, s_epd_stack, K_THREAD_STACK_SIZEOF(s_epd_stack),
			      epd_worker, NULL, NULL, NULL, 7, 0, K_NO_WAIT);
	k_thread_name_set(&s_epd_thread, "epd");
	printk("epd: worker started\n");
}

bool epd_board_is_ready(void)
{
	return s_ready;
}

status_t epd_board_text_request(const char *text, size_t len)
{
	struct epd_text_job job;

	if ((text == NULL) || (len == 0U)) {
		return STATUS_INVALID_ARG;
	}
	if (!s_started) {
		return STATUS_INVALID_STATE;
	}

	memset(&job, 0, sizeof(job));
	if (len > EPD_BOARD_TEXT_MAX) {
		len = EPD_BOARD_TEXT_MAX;
	}
	memcpy(job.text, text, len);
	job.text[len] = '\0';

	if (k_msgq_put(&s_epd_q, &job, K_NO_WAIT) != 0) {
		printk("epd: text queue full — rejected\n");
		return STATUS_NO_MEM;
	}
	return STATUS_OK;
}
