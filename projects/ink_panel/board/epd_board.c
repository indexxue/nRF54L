/**
 * @file epd_board.c
 * @brief ink_panel：EPD 路径（默认 GPIO bitbang；可选 SPIM20）
 *
 * 板级：DT 引脚 + nPM1300 供电。刷屏流程对齐 eink_tag。
 * SPIM：CONFIG_INK_PANEL_EPD_SPIM=y（当前 NCS 3.2 首次 spi_write 会 Bus Fault，默认关）。
 */

#include "epd_board.h"

#include "npm1300_board.h"

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>

#include <hal/nrf_spim.h>

#include "epd_display.h"
#include "epd_panel.h"

#define ZUSER DT_PATH(zephyr_user)

/** 0=GDEY029Z95 2.9"（与 eink_tag 默认冒烟一致）；1=WS 4.2" B V2 */
#ifndef INK_PANEL_EPD_USE_4IN2
#define INK_PANEL_EPD_USE_4IN2 1
#endif

#define EPD_PLANE_CAPACITY  15000U
#define EPD_PWR_SETTLE_MS   50U
#define EPD_BUSY_PROBE_MS   2000U
#define EPD_FRAME_SETTLE_MS 10000U /* BUSY 不可用时，对齐 eink_tag */
#define EPD_JOB_DEMO        1U
/*
 * SPIM20 core=16 MHz；用 8 MHz → PRESCALER=2。
 * Mode0 且 PRESCALER>2 时 nrfx 会写 SPIM+0xC84（errata 8），
 * 在本板实测会 Precise data bus error。8 MHz 跳过该 workaround。
 */
#define EPD_SPI_HZ          8000000U
#define EPD_SPI_CHUNK       64U

#if INK_PANEL_EPD_USE_4IN2
static const epd_panel_info_t *const s_panel = &epd_panel_ws_4in2b_v2;
static const char *const s_size_label = "4.2in EVT BWR";
#else
static const epd_panel_info_t *const s_panel = &epd_panel_gdey029z95;
static const char *const s_size_label = "2.9in EVT BWR";
#endif

/** 屏上标题：与 eink_tag 的 "eink_tag" 一眼可区分 */
static const char *const s_demo_title = "INK_PANEL EVT";

#if !IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
static const struct gpio_dt_spec s_sck = GPIO_DT_SPEC_GET(ZUSER, epd_sck_gpios);
static const struct gpio_dt_spec s_mosi = GPIO_DT_SPEC_GET(ZUSER, epd_mosi_gpios);
#endif
static const struct gpio_dt_spec s_cs = GPIO_DT_SPEC_GET(ZUSER, epd_cs_gpios);
static const struct gpio_dt_spec s_dc = GPIO_DT_SPEC_GET(ZUSER, epd_dc_gpios);
static const struct gpio_dt_spec s_rst = GPIO_DT_SPEC_GET(ZUSER, epd_rst_gpios);
static const struct gpio_dt_spec s_busy = GPIO_DT_SPEC_GET(ZUSER, epd_busy_gpios);

#if IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
static const struct device *const s_spi = DEVICE_DT_GET(DT_NODELABEL(spi20));
static struct spi_config s_spi_cfg;
static uint32_t s_spi_err_count;
static uint8_t s_spi_chunk[EPD_SPI_CHUNK];
static bool s_spi_first_done;
#endif

static uint8_t s_bw[EPD_PLANE_CAPACITY];
static uint8_t s_red[EPD_PLANE_CAPACITY];
static epd_display_t s_disp;
static bool s_ready;
static bool s_busy_usable;
static atomic_t s_refreshing;

K_THREAD_STACK_DEFINE(s_epd_stack, 4096);
static struct k_thread s_epd_thread;
static bool s_smoke_started;
K_MSGQ_DEFINE(s_epd_jobs, sizeof(uint8_t), 1, 1);

#if !IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
/* --- bitbang 回退：与 eink_tag 相同时序（2µs） --- */

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

static status_t epd_release_spim_pins(void)
{
	NRF_SPIM_Type *spim = (NRF_SPIM_Type *)DT_REG_ADDR(DT_NODELABEL(spi20));

	nrf_spim_disable(spim);
	spim->PSEL.SCK = 0xFFFFFFFFu;
	spim->PSEL.MOSI = 0xFFFFFFFFu;
	spim->PSEL.MISO = 0xFFFFFFFFu;
	printk("epd: SPIM20 pins disconnected for bitbang\n");
	return STATUS_OK;
}
#endif

static void epd_spi_tx(const uint8_t *data, uint16_t len)
{
	if ((data == NULL) || (len == 0U)) {
		return;
	}

#if IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
	{
		uint16_t off = 0U;

		/* 分块拷到静态 RAM，供 EasyDMA 使用 */
		while (off < len) {
			uint16_t n = len - off;
			struct spi_buf buf;
			const struct spi_buf_set tx = {
				.buffers = &buf,
				.count = 1U,
			};
			int err;

			if (n > EPD_SPI_CHUNK) {
				n = EPD_SPI_CHUNK;
			}
			memcpy(s_spi_chunk, &data[off], n);
			buf.buf = s_spi_chunk;
			buf.len = n;

			if (!s_spi_first_done) {
				printk("epd: first spi_write len=%u @%uHz\n", (unsigned)n,
				       (unsigned)EPD_SPI_HZ);
			}
			err = spi_write(s_spi, &s_spi_cfg, &tx);
			if (!s_spi_first_done) {
				s_spi_first_done = true;
				printk("epd: first spi_write done err=%d\n", err);
			}
			if (err != 0) {
				s_spi_err_count++;
				if (s_spi_err_count <= 5U) {
					printk("epd: spi_write len=%u err=%d\n", (unsigned)n, err);
				}
				return;
			}
			off = (uint16_t)(off + n);
		}
	}
#else
	for (uint16_t i = 0; i < len; i++) {
		epd_bb_tx_byte(data[i]);
		/* 大平面 bitbang 勿饿死 I2C/npm1300 work */
		if ((i & 0x3FU) == 0x3FU) {
			k_yield();
		}
	}
#endif
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
	npm1300_board_touch_power_set(true);
	k_msleep(EPD_PWR_SETTLE_MS);
}

static void epd_power_off(void)
{
	/* 冒烟阶段保持常供，对齐“常电刷屏” */
}

/** BUSY 卡住或超时后：改固定延时，避免一直 BUSY_TIMEOUT 只剩白屏 */
static void epd_busy_fallback_fixed(void)
{
	if (!s_busy_usable) {
		return;
	}
	s_busy_usable = false;
	s_disp.epd.read_busy = NULL;
	printk("epd: BUSY fallback -> fixed wait (~16s lib + %ums)\n",
	       (unsigned)EPD_FRAME_SETTLE_MS);
}

/**
 * 刷屏前：强制 EPD 轨上电；BUSY 已高则复位；仍高则放弃读 BUSY。
 */
static void epd_job_prepare(void)
{
	uint32_t waited = 0U;

	epd_power_on();
	printk("epd: prepare busy_raw=%d usable=%d\n", epd_busy(), s_busy_usable ? 1 : 0);
	npm1300_board_log_status();

	if (!s_busy_usable) {
		/* 固定延时模式：每次刷前复位控制器，避免上次超时半残 */
		(void)epd_init(&s_disp.epd);
		return;
	}
	if (epd_busy() == 0) {
		return;
	}

	printk("epd: BUSY stuck HIGH — rst + wait\n");
	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(20);
	while ((epd_busy() != 0) && (waited < EPD_BUSY_PROBE_MS)) {
		k_msleep(50);
		waited += 50U;
	}
	if (epd_busy() != 0) {
		/* 掉电重开一轮，再不行就固定延时 */
		npm1300_board_epd_power_set(false);
		k_msleep(100);
		epd_power_on();
		epd_rst(0);
		k_msleep(10);
		epd_rst(1);
		k_msleep(50);
		if (epd_busy() != 0) {
			epd_busy_fallback_fixed();
		}
	}
	(void)epd_init(&s_disp.epd);
}

static status_t epd_gpio_init(void)
{
#if IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
	if (!device_is_ready(s_spi)) {
		printk("epd: spi20 not ready\n");
		return STATUS_FAIL;
	}
	memset(&s_spi_cfg, 0, sizeof(s_spi_cfg));
	s_spi_cfg.frequency = EPD_SPI_HZ;
	s_spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;
	s_spi_err_count = 0U;
#else
	(void)epd_release_spim_pins();
	if (!gpio_is_ready_dt(&s_sck) || !gpio_is_ready_dt(&s_mosi)) {
		printk("epd gpio SCK/MOSI not ready\n");
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_sck, GPIO_OUTPUT_INACTIVE) != 0) {
		return STATUS_FAIL;
	}
	if (gpio_pin_configure_dt(&s_mosi, GPIO_OUTPUT_INACTIVE) != 0) {
		return STATUS_FAIL;
	}
#endif

	if (!gpio_is_ready_dt(&s_cs) || !gpio_is_ready_dt(&s_dc) ||
	    !gpio_is_ready_dt(&s_rst) || !gpio_is_ready_dt(&s_busy)) {
		printk("epd gpio not ready\n");
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
#if IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
	printk("epd: SPIM20 SCK=P1.08 MOSI=P1.09 CS=P1.10 @%uHz ready\n",
	       (unsigned)EPD_SPI_HZ);
#else
	printk("epd: bitbang SCK/MOSI/CS/DC/RST/BUSY ready\n");
#endif
	return STATUS_OK;
}

/**
 * 对齐 eink_tag：复位后应回到空闲；一直高 → 不用脚、改固定延时。
 * 复位后立刻为低是正常空闲，不能当成坏线。
 */
static bool epd_busy_probe(void)
{
	uint32_t waited = 0U;
	int raw_before;
	int raw_after;

	raw_before = epd_busy();
	printk("epd: BUSY raw=%d before rst (1=high)\n", raw_before);

	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(20);
	raw_after = epd_busy();
	printk("epd: BUSY raw=%d after rst\n", raw_after);

	while ((epd_busy() != 0) && (waited < EPD_BUSY_PROBE_MS)) {
		k_msleep(50);
		waited += 50U;
	}

	if (epd_busy() != 0) {
		printk("epd: BUSY stuck HIGH %ums — disable read_busy, use fixed wait\n",
		       (unsigned)waited);
		return false;
	}

	printk("epd: BUSY idle ok waited=%ums (usable)\n", (unsigned)waited);
	return true;
}

status_t epd_board_hw_scan(void)
{
	/* 保留符号：现与 bringup 合并，直接 OK（真正探测在 bringup） */
	return STATUS_OK;
}

status_t epd_board_bringup(void)
{
	epd_display_config_t cfg;
	epd_display_status_t st;

	s_ready = false;
	printk("epd bringup (%s)...\n",
	       IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM) ? "SPIM20" : "bitbang");

	if (epd_gpio_init() != STATUS_OK) {
		return STATUS_FAIL;
	}

	epd_power_on();
	epd_cs(1);
	s_busy_usable = epd_busy_probe();

	memset(&cfg, 0, sizeof(cfg));
	cfg.epd.spi_tx = epd_spi_tx;
	cfg.epd.set_cs = epd_cs;
	cfg.epd.set_dc = epd_dc;
	cfg.epd.set_rst = epd_rst;
	/* 与 eink_tag 相同：BUSY 可用才挂脚，否则 NULL → 库内 ~16s */
	cfg.epd.read_busy = s_busy_usable ? epd_busy : NULL;
	cfg.epd.delay_ms = epd_delay_ms;
	cfg.epd.busy_active_high = true;
	cfg.epd.bw_invert = false;
	cfg.epd.panel = s_panel;
	cfg.bw = s_bw;
	cfg.red = s_red;
	cfg.plane_capacity = EPD_PLANE_CAPACITY;
	cfg.power_on = epd_power_on;
	cfg.power_off = epd_power_off;
	cfg.hw_clear_on_init = false;

	st = epd_display_init(&s_disp, &cfg);
	if (st != EPD_DISPLAY_OK) {
		printk("epd: display_init failed st=%d busy=%d\n", (int)st, epd_busy());
		return STATUS_FAIL;
	}

	s_ready = true;
	printk("epd: ready panel=%s %ux%u path=%s bw_invert=0 busy_ok=%d settle=%ums\n",
	       s_panel->name, (unsigned)epd_display_width(&s_disp),
	       (unsigned)epd_display_height(&s_disp),
	       IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM) ? "spim20" : "bitbang",
	       s_busy_usable ? 1 : 0,
	       (unsigned)(s_busy_usable ? 0U : EPD_FRAME_SETTLE_MS));
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

/** 对齐 eink_tag epd_refresh_step */
static status_t epd_refresh_step(const char *name, epd_display_status_t (*fn)(epd_display_t *),
				 epd_color_t solid)
{
	int64_t t0;
	int64_t dt;
	epd_display_status_t st;

	t0 = k_uptime_get();
	if (fn != NULL) {
		st = fn(&s_disp);
	} else {
		epd_display_clear_buffer(&s_disp, solid);
		st = epd_display_refresh(&s_disp);
	}
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

	epd_job_prepare();

	/* 与 eink_tag 相同：先硬件擦白，再 BWR demo（文案区分产品） */
	printk("epd: erase screen before demo\n");
	st = epd_refresh_step("erase", epd_display_clear_screen, EPD_COLOR_WHITE);
	if (st != STATUS_OK) {
		printk("epd: erase failed — fallback BUSY + still try demo\n");
		epd_busy_fallback_fixed();
		epd_job_prepare();
	}

	printk("epd: draw BWR demo title=%s path=%s\n", s_demo_title,
	       IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM) ? "spim20" : "bitbang");
	epd_display_demo_bwr_fill(&s_disp, s_demo_title, s_size_label, s_panel->name);

	/* 盖掉库内共用的 "Hello nRF54L15!"，换成本板标识 */
	epd_display_fill_rect(&s_disp, 0U, 44U, (uint16_t)(epd_display_width(&s_disp) - 1U), 67U,
			      EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 48U,
				IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM) ? "ink_panel SPIM20"
								      : "ink_panel !=eink_tag",
				EPD_FONT_16, EPD_COLOR_BLACK);

	st = epd_refresh_step("demo", epd_display_refresh, EPD_COLOR_WHITE);
	if (st != STATUS_OK) {
		printk("epd: demo refresh failed — one more try after BUSY fallback\n");
		epd_busy_fallback_fixed();
		epd_job_prepare();
		st = epd_refresh_step("demo_retry", epd_display_refresh, EPD_COLOR_WHITE);
	}
	atomic_set(&s_refreshing, 0);
	if (st != STATUS_OK) {
		return st;
	}

	printk("epd: demo done — expect INK_PANEL EVT + B/R/W bands + != eink_tag\n");
	return STATUS_OK;
}

status_t epd_board_erase_run(void)
{
	/* 兼容旧 API：单独擦白 */
	status_t st;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_refreshing) == 0) {
		if (atomic_cas(&s_refreshing, 0, 1) == false) {
			return STATUS_INVALID_STATE;
		}
	}
	epd_job_prepare();
	st = epd_refresh_step("erase", epd_display_clear_screen, EPD_COLOR_WHITE);
	if (st != STATUS_OK) {
		epd_busy_fallback_fixed();
		epd_job_prepare();
		st = epd_refresh_step("erase_retry", epd_display_clear_screen, EPD_COLOR_WHITE);
	}
	atomic_set(&s_refreshing, 0);
	return st;
}

status_t epd_board_demo_request(void)
{
	uint8_t job = EPD_JOB_DEMO;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_refreshing) != 0) {
		return STATUS_INVALID_STATE;
	}
	if (k_msgq_put(&s_epd_jobs, &job, K_NO_WAIT) != 0) {
		return STATUS_FAIL;
	}
	return STATUS_OK;
}

status_t epd_board_erase_request(void)
{
	/* 按键统一走 demo（擦白+画面），与 eink_tag DISPLAY 一致 */
	return epd_board_demo_request();
}

static void epd_smoke_thread(void *a, void *b, void *c)
{
	status_t st;
	uint8_t job;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	printk("epd: worker start (eink_tag-aligned erase+demo)\n");

	st = epd_board_bringup();
	if (st != STATUS_OK) {
		printk("epd: bringup failed: %d\n", (int)st);
		return;
	}

	printk("epd: boot demo start\n");
	st = epd_board_test_run();
	if (st != STATUS_OK) {
		printk("epd: boot demo failed: %d\n", (int)st);
	} else {
		printk("epd: boot demo done\n");
	}

	printk("epd: idle — BTN1 click queues erase+demo\n");

	while (1) {
		if (k_msgq_get(&s_epd_jobs, &job, K_FOREVER) != 0) {
			continue;
		}
		if (job != EPD_JOB_DEMO) {
			continue;
		}
		printk("epd: queued demo start\n");
		st = epd_board_test_run();
		printk("epd: queued demo %s\n", (st == STATUS_OK) ? "done" : "failed");
	}
}

void epd_board_smoke_start(void)
{
	if (s_smoke_started) {
		return;
	}
	s_smoke_started = true;
	atomic_set(&s_refreshing, 0);
	(void)k_msgq_purge(&s_epd_jobs);
	(void)k_thread_create(&s_epd_thread, s_epd_stack, K_THREAD_STACK_SIZEOF(s_epd_stack),
			      epd_smoke_thread, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	k_thread_name_set(&s_epd_thread, "epd");
	printk("epd: async worker created\n");
}
