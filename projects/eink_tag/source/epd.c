/**
 * @file epd.c
 * @brief 墨水屏胶水：默认 GPIO bit-bang SPI（绕过 SPIM00）；DC/RST/BUSY 读 DT。
 *
 * 说明：MOSI/MISO 绑同一脚的“回环”在 SPIM 上无效（脚只能做输出），rx=0 不能证明 TX 失败。
 */

#include "epd_board.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <hal/nrf_spim.h>

#include "board.h"
#include "epd_display.h"
#include "epd_panel.h"
#include "log.h"

#define ZUSER DT_PATH(zephyr_user)

#define EPD_PLANE_SIZE 4736U /* 128×296 / 8 */
#define EPD_BUSY_PROBE_MS 2000U
/** BUSY 不可用时每帧后再等（单帧 demo 给足波形时间） */
#define EPD_FRAME_SETTLE_MS 10000U

/** 1=GPIO bit-bang（推荐先验证接线）；0=走 SPIM00 */
#ifndef EPD_USE_BITBANG
#define EPD_USE_BITBANG 1
#endif

#define EPD_SCK_PIN  1U
#define EPD_MOSI_PIN 4U

static const struct gpio_dt_spec s_dc = GPIO_DT_SPEC_GET(ZUSER, epd_dc_gpios);
static const struct gpio_dt_spec s_rst = GPIO_DT_SPEC_GET(ZUSER, epd_rst_gpios);
static const struct gpio_dt_spec s_busy = GPIO_DT_SPEC_GET(ZUSER, epd_busy_gpios);
static const struct device *s_gpio2 = DEVICE_DT_GET(DT_NODELABEL(gpio2));

static const board_spi_slave_spec_t *s_slave;
static const board_spi_bus_spec_t *s_bus;
static struct spi_config s_spi_cfg;
static bool s_bitbang;

static uint8_t s_bw[EPD_PLANE_SIZE];
static uint8_t s_red[EPD_PLANE_SIZE];
static epd_display_t s_disp;
static bool s_ready;
static bool s_busy_usable = true;
static uint32_t s_spi_err_count;
static atomic_t s_refreshing;

static status_t epd_board_guard_writable(void)
{
	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_refreshing) != 0) {
		return STATUS_INVALID_STATE;
	}
	return STATUS_OK;
}

static const board_spi_slave_spec_t *epd_find_slave(void)
{
	const uint8_t n = board_spi_slave_count();

	for (uint8_t i = 0; i < n; i++) {
		const board_spi_slave_spec_t *s = board_spi_slave_spec(i);

		if ((s != NULL) && (s->name != NULL) && (strcmp(s->name, "epd") == 0)) {
			return s;
		}
	}
	return NULL;
}

static void epd_bb_delay(void)
{
	/* ~数 µs，Mode0 写屏足够慢、便于示波器观察 */
	k_busy_wait(2);
}

static void epd_bb_tx_byte(uint8_t byte)
{
	for (int8_t bit = 7; bit >= 0; bit--) {
		(void)gpio_pin_set_raw(s_gpio2, EPD_MOSI_PIN, (byte >> bit) & 0x1);
		epd_bb_delay();
		(void)gpio_pin_set_raw(s_gpio2, EPD_SCK_PIN, 1); /* rising: sample */
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

	if (s_bitbang) {
		for (uint16_t i = 0; i < len; i++) {
			epd_bb_tx_byte(data[i]);
		}
		return;
	}

	const struct spi_buf buf = {
		.buf = (void *)data,
		.len = len,
	};
	const struct spi_buf_set tx = {
		.buffers = &buf,
		.count = 1U,
	};
	int err;

	if ((s_bus == NULL) || (s_bus->dev == NULL)) {
		return;
	}

	err = spi_write(s_bus->dev, &s_spi_cfg, &tx);
	if (err != 0) {
		s_spi_err_count++;
		if (s_spi_err_count <= 5U) {
			LOG_ERROR("epd: spi_write len=%u err=%d", (unsigned int)len, err);
		}
	}
}

static void epd_cs(int high)
{
	if ((s_slave == NULL) || (s_slave->cs == NULL)) {
		return;
	}
	(void)gpio_pin_set_raw(s_slave->cs->port, s_slave->cs->pin, high ? 1 : 0);
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

static status_t epd_release_spim_pins(void)
{
	NRF_SPIM_Type *spim = (NRF_SPIM_Type *)DT_REG_ADDR(DT_NODELABEL(spi00));

	/* 断开 SPIM 对脚占用，便于改成 GPIO bitbang */
	nrf_spim_disable(spim);
	spim->PSEL.SCK = 0xFFFFFFFFu;
	spim->PSEL.MOSI = 0xFFFFFFFFu;
	spim->PSEL.MISO = 0xFFFFFFFFu;
	LOG_INFO("epd: SPIM00 pins disconnected for bitbang");
	return STATUS_OK;
}

static status_t epd_bb_claim_pins(void)
{
	if (!device_is_ready(s_gpio2)) {
		LOG_ERROR("epd: gpio2 not ready");
		return STATUS_FAIL;
	}

	if (gpio_pin_configure(s_gpio2, EPD_SCK_PIN, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure(s_gpio2, EPD_MOSI_PIN, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_ERROR("epd: SCK/MOSI GPIO configure failed");
		return STATUS_FAIL;
	}

	s_bitbang = true;
	LOG_INFO("epd: bitbang SPI on SCK=P2.%u MOSI=P2.%u CS=P2.05", EPD_SCK_PIN, EPD_MOSI_PIN);
	return STATUS_OK;
}

static status_t epd_bind_bus(void)
{
	s_slave = epd_find_slave();
	if (s_slave == NULL) {
		LOG_ERROR("epd: spi slave \"epd\" missing in board_config");
		return STATUS_INVALID_STATE;
	}

	s_bus = board_spi_bus_spec(s_slave->bus_index);
	if ((s_bus == NULL) || (s_bus->dev == NULL) || !device_is_ready(s_bus->dev)) {
		LOG_ERROR("epd: spi bus not ready");
		return STATUS_FAIL;
	}

	if ((s_slave->cs == NULL) || !gpio_is_ready_dt(s_slave->cs) || !gpio_is_ready_dt(&s_dc) ||
	    !gpio_is_ready_dt(&s_rst) || !gpio_is_ready_dt(&s_busy)) {
		LOG_ERROR("epd: GPIO not ready");
		return STATUS_FAIL;
	}

	if (gpio_pin_configure_dt(s_slave->cs, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_dc, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_rst, GPIO_OUTPUT_ACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_busy, GPIO_INPUT | GPIO_PULL_UP) != 0) {
		LOG_ERROR("epd: GPIO configure failed");
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

static void epd_spi_dump_regs(const char *tag)
{
	NRF_SPIM_Type *spim = (NRF_SPIM_Type *)DT_REG_ADDR(DT_NODELABEL(spi00));

	LOG_INFO("epd: SPIM00[%s] ENABLE=0x%x PSEL SCK=0x%x MOSI=0x%x MISO=0x%x PRESCALER=0x%x",
		 tag, (unsigned int)spim->ENABLE, (unsigned int)spim->PSEL.SCK,
		 (unsigned int)spim->PSEL.MOSI, (unsigned int)spim->PSEL.MISO,
		 (unsigned int)spim->PRESCALER);
}

status_t epd_board_spi_selftest(void)
{
	status_t st;
	uint32_t bitrate;
	uint8_t pat[4] = {0xA5U, 0x5AU, 0xF0U, 0x0FU};

	st = epd_bind_bus();
	if (st != STATUS_OK) {
		return st;
	}

	LOG_INFO("epd: SPI SELFTEST begin dev=%s ready=%d", s_bus->dev->name,
		 device_is_ready(s_bus->dev) ? 1 : 0);
	epd_spi_dump_regs("boot");

	bitrate = (s_slave->bitrate_hz != 0U) ? s_slave->bitrate_hz : s_bus->bitrate_hz;
	if (bitrate == 0U) {
		bitrate = 4000000U;
	}
	memset(&s_spi_cfg, 0, sizeof(s_spi_cfg));
	s_spi_cfg.frequency = bitrate;
	s_spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;

#if EPD_USE_BITBANG
	LOG_INFO("epd: SELFTEST use GPIO bitbang (ignore same-pin loopback)");
	(void)epd_release_spim_pins();
	st = epd_bb_claim_pins();
	if (st != STATUS_OK) {
		return st;
	}

	LOG_INFO("epd: SELFTEST toggle SCK/MOSI/CS 4x (measure with DMM/scope)");
	for (int i = 0; i < 4; i++) {
		(void)gpio_pin_set_raw(s_gpio2, EPD_SCK_PIN, 0);
		(void)gpio_pin_set_raw(s_gpio2, EPD_MOSI_PIN, 0);
		epd_cs(0);
		LOG_INFO("epd: bb LOW  #%d", i);
		k_msleep(400);
		(void)gpio_pin_set_raw(s_gpio2, EPD_SCK_PIN, 1);
		(void)gpio_pin_set_raw(s_gpio2, EPD_MOSI_PIN, 1);
		epd_cs(1);
		LOG_INFO("epd: bb HIGH #%d", i);
		k_msleep(400);
	}

	epd_cs(0);
	epd_spi_tx(pat, sizeof(pat));
	epd_cs(1);
	LOG_INFO("epd: SELFTEST bitbang burst done — SPI path READY (bitbang)");
#else
	{
		const struct spi_buf buf = {.buf = pat, .len = sizeof(pat)};
		const struct spi_buf_set tx = {.buffers = &buf, .count = 1U};
		int err;

		epd_cs(0);
		err = spi_write(s_bus->dev, &s_spi_cfg, &tx);
		epd_cs(1);
		if (err != 0) {
			LOG_ERROR("epd: SELFTEST spi_write err=%d", err);
			return STATUS_FAIL;
		}
		epd_spi_dump_regs("after write");
		LOG_INFO("epd: SELFTEST SPIM write OK (no RX check)");
	}
#endif

	LOG_INFO("epd: SPI SELFTEST PASS");
	return STATUS_OK;
}

/**
 * 复位后观察 BUSY：正常应回到空闲（active-high → 低）。
 * 一直为高 → 不可用（改固定延时）。复位后立刻为低是正常空闲，不能当成“坏线”。
 */
static bool epd_busy_probe(void)
{
	uint32_t waited = 0U;
	int raw_before;
	int raw_after;

	raw_before = epd_busy();
	LOG_INFO("epd: BUSY raw=%d before rst (1=high)", raw_before);

	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(20);
	raw_after = epd_busy();
	LOG_INFO("epd: BUSY raw=%d after rst", raw_after);

	while ((epd_busy() != 0) && (waited < EPD_BUSY_PROBE_MS)) {
		k_msleep(50);
		waited += 50U;
	}

	if (epd_busy() != 0) {
		LOG_WARN("epd: BUSY stuck HIGH %ums — disable read_busy, use fixed wait",
			 (unsigned int)waited);
		return false;
	}

	LOG_INFO("epd: BUSY idle ok waited=%ums (usable)", (unsigned int)waited);
	return true;
}

status_t epd_board_init(void)
{
	epd_display_config_t cfg;
	epd_display_status_t st;
	uint32_t bitrate;

	s_ready = false;
	s_spi_err_count = 0U;

	if (epd_bind_bus() != STATUS_OK) {
		return STATUS_FAIL;
	}

#if EPD_USE_BITBANG
	(void)epd_release_spim_pins();
	if (epd_bb_claim_pins() != STATUS_OK) {
		return STATUS_FAIL;
	}
#else
	s_bitbang = false;
#endif

	epd_cs(1);
	s_busy_usable = epd_busy_probe();

	bitrate = (s_slave->bitrate_hz != 0U) ? s_slave->bitrate_hz : s_bus->bitrate_hz;
	if (bitrate == 0U) {
		bitrate = 4000000U;
	}

	memset(&s_spi_cfg, 0, sizeof(s_spi_cfg));
	s_spi_cfg.frequency = bitrate;
	s_spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;

	memset(&cfg, 0, sizeof(cfg));
	cfg.epd.spi_tx = epd_spi_tx;
	cfg.epd.set_cs = epd_cs;
	cfg.epd.set_dc = epd_dc;
	cfg.epd.set_rst = epd_rst;
	/*
	 * BUSY 可用才挂 read_busy。不可用时必须 NULL，否则 wait_idle 仍会轮询引脚，
	 * BWR 全刷 >8s 易 EPD_ERROR_BUSY；NULL 走库内固定 ~16s 延时。
	 */
	cfg.epd.read_busy = s_busy_usable ? epd_busy : NULL;
	cfg.epd.delay_ms = epd_delay_ms;
	cfg.epd.busy_active_high = true;
	/* 全黑常见于 invert 与模组不符；本板实测先关 */
	cfg.epd.bw_invert = false;
	cfg.epd.panel = &epd_panel_gdey029z95;
	cfg.bw = s_bw;
	cfg.red = s_red;
	cfg.plane_capacity = EPD_PLANE_SIZE;
	cfg.hw_clear_on_init = false;

	st = epd_display_init(&s_disp, &cfg);
	if (st != EPD_DISPLAY_OK) {
		LOG_ERROR("epd: display init failed st=%d spi_err=%u busy=%d", (int)st,
			  (unsigned int)s_spi_err_count, epd_busy());
		return STATUS_FAIL;
	}

	s_ready = true;
	LOG_INFO("epd: ready panel=%s %ux%u path=%s bw_invert=0 busy_ok=%d settle=%ums",
		 epd_panel_gdey029z95.name, (unsigned int)epd_display_width(&s_disp),
		 (unsigned int)epd_display_height(&s_disp), s_bitbang ? "bitbang" : "spim00",
		 s_busy_usable ? 1 : 0, (unsigned int)(s_busy_usable ? 0U : EPD_FRAME_SETTLE_MS));
	return STATUS_OK;
}

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
		st = epd_display_refresh(&s_disp); /* 始终双平面，避免红层残留叠影 */
	}
	if (!s_busy_usable) {
		k_msleep(EPD_FRAME_SETTLE_MS);
	}
	dt = k_uptime_get() - t0;

	LOG_INFO("epd: %s st=%d(%s) dt=%lldms busy_ok=%d", name, (int)st,
		 (st == EPD_DISPLAY_OK)             ? "ok"
		 : (st == EPD_DISPLAY_ERROR_BUSY)   ? "BUSY_TIMEOUT"
		 : (st == EPD_DISPLAY_ERROR_PARAM)  ? "PARAM"
		 : (st == EPD_DISPLAY_ERROR_NOT_INIT) ? "NOT_INIT"
						      : "ERR",
		 (long long)dt, s_busy_usable ? 1 : 0);

	return (st == EPD_DISPLAY_OK) ? STATUS_OK : STATUS_FAIL;
}

status_t epd_board_test_run(void)
{
	status_t st;
	const uint16_t w = epd_display_width(&s_disp);
	const uint16_t h = epd_display_height(&s_disp);
	const uint16_t x_max = (uint16_t)(w - 1U);
	const uint16_t y_max = (uint16_t)(h - 1U);
	const uint16_t col_w = (uint16_t)(w / 3U);
	const uint16_t title_h = 40U;
	const uint16_t band_y0 = 72U;
	const uint16_t band_y1 = (uint16_t)(h - 56U);
	const uint16_t label_y = (uint16_t)(band_y0 + 16U);
	const uint16_t sample_y = (uint16_t)(band_y0 + 48U);

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}

	if (atomic_get(&s_refreshing) == 0) {
		if (atomic_cas(&s_refreshing, 0, 1) == false) {
			LOG_WARN("epd: demo skipped (refresh busy)");
			return STATUS_INVALID_STATE;
		}
	}

	/* 单帧：白底 + 三色块 + 字符（只刷一次） */
	LOG_INFO("epd: draw BWR demo (chars + black/red/white), one refresh");

	epd_display_clear_buffer(&s_disp, EPD_COLOR_WHITE);

	/* 顶栏黑底白字 */
	epd_display_fill_rect(&s_disp, 0U, 0U, x_max, (uint16_t)(title_h - 1U), EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, 6U, "eink_tag", EPD_FONT_16, EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 24U, "2.9in BWR demo", EPD_FONT_12, EPD_COLOR_WHITE);

	epd_display_draw_string(&s_disp, 4U, 48U, "Hello nRF54L15!", EPD_FONT_16, EPD_COLOR_BLACK);

	/* 三列色块 */
	epd_display_fill_rect(&s_disp, 0U, band_y0, (uint16_t)(col_w - 2U), band_y1, EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, label_y, "BLACK", EPD_FONT_12, EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, sample_y, "ABC", EPD_FONT_16, EPD_COLOR_WHITE);

	epd_display_fill_rect(&s_disp, col_w, band_y0, (uint16_t)(col_w * 2U - 2U), band_y1,
			      EPD_COLOR_RED);
	epd_display_draw_string(&s_disp, (uint16_t)(col_w + 4U), label_y, "RED", EPD_FONT_12,
				EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, (uint16_t)(col_w + 4U), sample_y, "123", EPD_FONT_16,
				EPD_COLOR_WHITE);

	epd_display_fill_rect(&s_disp, (uint16_t)(col_w * 2U), band_y0, x_max, band_y1,
			      EPD_COLOR_WHITE);
	epd_display_draw_rect(&s_disp, (uint16_t)(col_w * 2U), band_y0, x_max, band_y1,
			      EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, (uint16_t)(col_w * 2U + 2U), label_y, "WHITE", EPD_FONT_12,
				EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, (uint16_t)(col_w * 2U + 2U), sample_y, "XYZ", EPD_FONT_16,
				EPD_COLOR_BLACK);

	epd_display_draw_string(&s_disp, 4U, (uint16_t)(band_y1 + 8U), "BLACK / RED / WHITE",
				EPD_FONT_12, EPD_COLOR_BLACK);
	epd_display_draw_string(&s_disp, 4U, (uint16_t)(band_y1 + 24U), "GDEY029Z95 OK",
				EPD_FONT_12, EPD_COLOR_RED);
	epd_display_draw_rect(&s_disp, 0U, 0U, x_max, y_max, EPD_COLOR_BLACK);

	st = epd_refresh_step("demo", epd_display_refresh, EPD_COLOR_WHITE);
	atomic_set(&s_refreshing, 0);
	if (st != STATUS_OK) {
		return st;
	}

	LOG_INFO("epd: demo done — should show text + 3 colors");
	return STATUS_OK;
}

status_t epd_board_lock_planes(void)
{
	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_cas(&s_refreshing, 0, 1) == false) {
		return STATUS_INVALID_STATE;
	}
	return STATUS_OK;
}

void epd_board_unlock_planes(void)
{
	atomic_set(&s_refreshing, 0);
}

status_t epd_board_refresh(void)
{
	status_t st;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}

	if (atomic_get(&s_refreshing) == 0) {
		if (atomic_cas(&s_refreshing, 0, 1) == false) {
			LOG_WARN("epd: refresh skipped (busy)");
			return STATUS_INVALID_STATE;
		}
	}

	LOG_INFO("epd: host buffer refresh");
	st = epd_refresh_step("host", epd_display_refresh, EPD_COLOR_WHITE);
	atomic_set(&s_refreshing, 0);
	return st;
}

bool epd_board_is_ready(void)
{
	return s_ready && epd_display_is_ready(&s_disp);
}

bool epd_board_is_busy(void)
{
	return atomic_get(&s_refreshing) != 0;
}

uint16_t epd_board_width(void)
{
	return s_ready ? epd_display_width(&s_disp) : 0U;
}

uint16_t epd_board_height(void)
{
	return s_ready ? epd_display_height(&s_disp) : 0U;
}

uint32_t epd_board_plane_size(void)
{
	return EPD_PLANE_SIZE;
}

status_t epd_board_clear_planes(uint8_t planes_mask)
{
	status_t st = epd_board_guard_writable();

	if (st != STATUS_OK) {
		return st;
	}
	if ((planes_mask & (EPD_BOARD_PLANES_BW | EPD_BOARD_PLANES_RED)) == 0U) {
		return STATUS_INVALID_ARG;
	}

	if ((planes_mask & EPD_BOARD_PLANES_BW) != 0U) {
		memset(s_bw, 0xFF, sizeof(s_bw)); /* 1 = white */
	}
	if ((planes_mask & EPD_BOARD_PLANES_RED) != 0U) {
		memset(s_red, 0x00, sizeof(s_red));
	}
	return STATUS_OK;
}

status_t epd_board_write_plane(uint8_t plane, uint16_t offset, const uint8_t *data, uint16_t len)
{
	uint8_t *dst;
	status_t st = epd_board_guard_writable();

	if (st != STATUS_OK) {
		return st;
	}
	if ((data == NULL) || (len == 0U)) {
		return STATUS_INVALID_ARG;
	}
	if (((uint32_t)offset + (uint32_t)len) > EPD_PLANE_SIZE) {
		return STATUS_INVALID_ARG;
	}

	if (plane == EPD_BOARD_PLANE_BW) {
		dst = s_bw;
	} else if (plane == EPD_BOARD_PLANE_RED) {
		dst = s_red;
	} else {
		return STATUS_INVALID_ARG;
	}

	memcpy(dst + offset, data, len);
	return STATUS_OK;
}

const uint8_t *epd_board_plane_data(uint8_t plane)
{
	if (!s_ready) {
		return NULL;
	}
	if (plane == EPD_BOARD_PLANE_BW) {
		return s_bw;
	}
	if (plane == EPD_BOARD_PLANE_RED) {
		return s_red;
	}
	return NULL;
}
