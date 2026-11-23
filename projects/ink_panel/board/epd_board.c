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
#include <zephyr/sys/util.h>

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
#include "page_slot.h"
#endif

#include <hal/nrf_spim.h>

#include "epd_display.h"
#include "epd_panel.h"

#define ZUSER DT_PATH(zephyr_user)

/** 缓冲按最大面板（4.2" 400×300 / 8 = 15000）；active plane 随 NVS 选屏 */
#define EPD_PLANE_CAPACITY  15000U
#define EPD_PWR_SETTLE_MS   80U
#define EPD_PWR_OFF_MS      100U
#define EPD_BUSY_PROBE_MS   2000U
#define EPD_FRAME_SETTLE_MS 10000U /* BUSY 不可用时，对齐 eink_tag */
/* 4.2" BWR 全刷通常 >12s；仅写 RAM+假 BUSY 约 1–3s */
#define EPD_REFRESH_MIN_MS  5000U
/** Auto：无红快路径累计 N 次后强制全刷清残影 */
#define EPD_FULL_EVERY_N    8U
#define EPD_JOB_DEMO           1U
#define EPD_JOB_HOST           2U
#define EPD_JOB_SHOW_SLOT      3U
#define EPD_JOB_STORE_REFRESH  4U
#define EPD_JOB_STORE_ONLY     5U

typedef struct {
	uint8_t type;
	uint8_t mode;
	uint8_t page;
	uint8_t show; /* STORE_*：1=刷后写槽；0=只写槽 */
} epd_job_t;
/*
 * SPIM20 core=16 MHz；用 8 MHz → PRESCALER=2。
 * Mode0 且 PRESCALER>2 时 nrfx 会写 SPIM+0xC84（errata 8），
 * 在本板实测会 Precise data bus error。8 MHz 跳过该 workaround。
 */
#define EPD_SPI_HZ          8000000U
#define EPD_SPI_CHUNK       64U

/** 屏上标题：与 eink_tag 的 "eink_tag" 一眼可区分 */
static const char *const s_demo_title = "INK_PANEL EVT";

static const epd_panel_info_t *s_panel;
static uint8_t s_size = EPD_BOARD_SIZE_4IN2;
static uint32_t s_plane_size = EPD_PLANE_CAPACITY;
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
#endif

static uint8_t s_bw[EPD_PLANE_CAPACITY];
static uint8_t s_red[EPD_PLANE_CAPACITY];
static epd_display_t s_disp;
static bool s_ready;
static bool s_busy_usable;
static bool s_panel_asleep;    /* 上次 epd_sleep 后须再 init */
static uint32_t s_fast_since_full;
static atomic_t s_plane_lock;  /* 主机平面：CHUNK / 推显存 */
static atomic_t s_panel_busy;  /* 面板物理全刷 / sleep */
static atomic_t s_rails_suspended;

K_THREAD_STACK_DEFINE(s_epd_stack, 4096);
static struct k_thread s_epd_thread;
static bool s_worker_started;
K_MSGQ_DEFINE(s_epd_jobs, sizeof(epd_job_t), 2, 4);

static const epd_panel_info_t *epd_board_pick_panel(uint8_t size)
{
	ARG_UNUSED(size);
	/* γ：产品仅 4.2"；已从镜像剔除 2.9" 面板源 */
	return &epd_panel_ws_4in2b_v2;
}

static status_t epd_board_guard_writable(void)
{
	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_plane_lock) != 0) {
		return STATUS_INVALID_STATE;
	}
	return STATUS_OK;
}

static status_t epd_board_queue_job(const epd_job_t *job)
{
	if (!s_ready) {
		printk("epd: queue job rejected - not ready\n");
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_rails_suspended) != 0) {
		printk("epd: queue job rejected - rails suspended\n");
		return STATUS_INVALID_STATE;
	}
	if (job == NULL) {
		return STATUS_INVALID_ARG;
	}
	/* 不在入队时占 plane_lock：否则第二张排队会堵住下一张 BEGIN。
	 * 缓冲冻结靠「msgq 非空 / plane_lock」；worker 取到 job 再加锁。
	 */
	if (k_msgq_put(&s_epd_jobs, job, K_NO_WAIT) != 0) {
		printk("epd: queue full type=%u\n", (unsigned)job->type);
		return STATUS_FAIL;
	}
	return STATUS_OK;
}

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

			err = spi_write(s_spi, &s_spi_cfg, &tx);
			if (err != 0) {
				s_spi_err_count++;
				if (s_spi_err_count <= 3U) {
					printk("epd: spi err=%d\n", err);
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
	/* 只开 EPD 轨；触摸电源由 CAP 路径单独管 */
	npm1300_board_epd_power_set(true);
	k_msleep(EPD_PWR_SETTLE_MS);
}

static void epd_power_off(void)
{
	/* 驱动回调：面板 deep-sleep 后不关轨；硬关只走 prepare / rails_suspend */
}

/** 关轨前把 MCU 脚改为输入，避免灌入未供电面板 */
static void epd_pins_park(void)
{
#if !IS_ENABLED(CONFIG_INK_PANEL_EPD_SPIM)
	if (gpio_is_ready_dt(&s_sck)) {
		(void)gpio_pin_configure_dt(&s_sck, GPIO_INPUT);
	}
	if (gpio_is_ready_dt(&s_mosi)) {
		(void)gpio_pin_configure_dt(&s_mosi, GPIO_INPUT);
	}
#endif
	if (gpio_is_ready_dt(&s_cs)) {
		(void)gpio_pin_configure_dt(&s_cs, GPIO_INPUT);
	}
	if (gpio_is_ready_dt(&s_dc)) {
		(void)gpio_pin_configure_dt(&s_dc, GPIO_INPUT);
	}
	if (gpio_is_ready_dt(&s_rst)) {
		(void)gpio_pin_configure_dt(&s_rst, GPIO_INPUT);
	}
	/* BUSY 保持输入上拉 */
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

static bool epd_red_plane_active(void)
{
	uint32_t i;

	for (i = 0U; i < s_plane_size; i++) {
		if (s_red[i] != 0U) {
			return true;
		}
	}
	return false;
}

/** Auto：有红或累计快路径次数到顶 → 全刷路径（硬上电+init） */
static bool epd_want_full_prepare(void)
{
	if (epd_red_plane_active()) {
		return true;
	}
	if (s_fast_since_full >= EPD_FULL_EVERY_N) {
		return true;
	}
	return false;
}

/**
 * 刷屏前准备。force_rail：硬电源循环（全刷/异常）。
 * 快路径：轨已好且未深睡 → 跳过 power-cycle/init，收完包后尽快 kick。
 * @return false：轨已休眠挂起，调用方应中止本次刷屏
 */
static bool epd_job_prepare(bool force_rail)
{
	uint32_t waited = 0U;
	epd_status_t ist;
	int rail_try;
	bool rail_ok;
	bool need_init;

	npm1300_board_charge_inhibit_set(true);

	/* 休眠关轨后禁止在 worker 内偷偷 resume；只许 app_pm_exit_sleep */
	if (atomic_get(&s_rails_suspended) != 0) {
		printk("epd: prepare aborted (rails suspended)\n");
		npm1300_board_charge_inhibit_set(false);
		return false;
	}

	rail_ok = npm1300_board_epd_rail_ok();
	need_init = s_panel_asleep || !epd_is_initialized(&s_disp.epd);

	if (force_rail || !rail_ok) {
		npm1300_board_epd_power_set(false);
		k_msleep(EPD_PWR_OFF_MS);
		for (rail_try = 0; rail_try < 2; rail_try++) {
			epd_power_on();
			if (npm1300_board_epd_rail_ok()) {
				break;
			}
			npm1300_board_epd_power_set(false);
			k_msleep(EPD_PWR_OFF_MS);
		}
		need_init = true;
	} else {
		epd_power_on();
	}

	if (s_busy_usable && (epd_busy() != 0)) {
		epd_rst(0);
		k_msleep(10);
		epd_rst(1);
		k_msleep(20);
		while ((epd_busy() != 0) && (waited < EPD_BUSY_PROBE_MS)) {
			k_msleep(50);
			waited += 50U;
		}
		if (epd_busy() != 0) {
			npm1300_board_epd_power_set(false);
			k_msleep(EPD_PWR_OFF_MS);
			epd_power_on();
			epd_rst(0);
			k_msleep(10);
			epd_rst(1);
			k_msleep(50);
			need_init = true;
			if (epd_busy() != 0) {
				epd_busy_fallback_fixed();
			}
		}
	}

	if (need_init) {
		ist = epd_init(&s_disp.epd);
		s_panel_asleep = false;
		if (ist != EPD_OK) {
			printk("epd: init fail st=%d\n", (int)ist);
		}
		k_msleep(20);
	}
	return true;
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
	return STATUS_OK;
}

/**
 * 对齐 eink_tag：复位后应回到空闲；一直高 → 不用脚、改固定延时。
 * 复位后立刻为低是正常空闲，不能当成坏线。
 */
static bool epd_busy_probe(void)
{
	uint32_t waited = 0U;

	epd_rst(0);
	k_msleep(10);
	epd_rst(1);
	k_msleep(20);

	while ((epd_busy() != 0) && (waited < EPD_BUSY_PROBE_MS)) {
		k_msleep(50);
		waited += 50U;
	}

	if (epd_busy() != 0) {
		printk("epd: BUSY unusable — fixed wait\n");
		return false;
	}
	return true;
}

status_t epd_board_bringup(void)
{
	epd_display_config_t cfg;
	epd_display_status_t st;
	uint32_t plane;

	s_ready = false;

	/* 产品仅 4.2"；忽略历史 NVS 2.9 值 */
	s_size = EPD_BOARD_SIZE_4IN2;
	s_panel = epd_board_pick_panel(s_size);
	plane = epd_panel_plane_size(s_panel);
	if ((plane == 0U) || (plane > EPD_PLANE_CAPACITY)) {
		printk("epd: bad plane size %u for %s\n", (unsigned)plane,
		       (s_panel != NULL) ? s_panel->name : "?");
		return STATUS_FAIL;
	}
	s_plane_size = plane;

	if (epd_gpio_init() != STATUS_OK) {
		return STATUS_FAIL;
	}

	epd_power_on();
	npm1300_board_touch_power_set(true);
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
	return STATUS_OK;
}

bool epd_board_is_ready(void)
{
	return s_ready && epd_display_is_ready(&s_disp);
}

bool epd_board_planes_busy(void)
{
	/* 推显存中，或已有 job 排队占用当前平面内容 */
	return (atomic_get(&s_plane_lock) != 0) ||
	       (k_msgq_num_used_get(&s_epd_jobs) > 0);
}

bool epd_board_panel_busy(void)
{
	return atomic_get(&s_panel_busy) != 0;
}

bool epd_board_is_busy(void)
{
	return epd_board_planes_busy() || epd_board_panel_busy();
}

bool epd_board_rails_suspended(void)
{
	return atomic_get(&s_rails_suspended) != 0;
}

status_t epd_board_rails_suspend(void)
{
	epd_job_t drop;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_rails_suspended) != 0) {
		return STATUS_OK;
	}

	/* 先占位拒入队，再复查 busy，缩小与 worker 的窗口 */
	atomic_set(&s_rails_suspended, 1);
	if (epd_board_is_busy()) {
		atomic_set(&s_rails_suspended, 0);
		return STATUS_INVALID_STATE;
	}
	while (k_msgq_get(&s_epd_jobs, &drop, K_NO_WAIT) == 0) {
		/* 排空残留 job，避免睡后再跑 */
	}

	/* 有轨且已 init：先 deep-sleep 再断轨 */
	if (npm1300_board_epd_rail_ok() && epd_is_initialized(&s_disp.epd) &&
	    !s_panel_asleep) {
		(void)epd_sleep(&s_disp.epd);
		s_panel_asleep = true;
	}

	epd_pins_park();
	npm1300_board_epd_power_set(false);
	if (npm1300_board_epd_rail_ok()) {
		printk("epd: rails suspend FAIL (LDO2 still on)\n");
		atomic_set(&s_rails_suspended, 0);
		(void)epd_gpio_init();
		return STATUS_FAIL;
	}
	printk("epd: rails suspend\n");
	return STATUS_OK;
}

status_t epd_board_rails_resume(void)
{
	status_t st;

	if (atomic_get(&s_rails_suspended) == 0) {
		return STATUS_OK;
	}

	st = epd_gpio_init();
	if (st != STATUS_OK) {
		printk("epd: rails resume gpio fail\n");
		return st;
	}

	epd_power_on();
	s_panel_asleep = true;
	atomic_set(&s_rails_suspended, 0);
	printk("epd: rails resume\n");
	return STATUS_OK;
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

	if (st != EPD_DISPLAY_OK) {
		printk("epd: %s fail st=%d dt=%lldms\n", name, (int)st, (long long)dt);
	}
	return (st == EPD_DISPLAY_OK) ? STATUS_OK : STATUS_FAIL;
}

status_t epd_board_test_run(void)
{
	status_t st;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}

	if (atomic_get(&s_plane_lock) == 0) {
		if (atomic_cas(&s_plane_lock, 0, 1) == false) {
			return STATUS_INVALID_STATE;
		}
	}
	atomic_set(&s_panel_busy, 1);

	if (!epd_job_prepare(true)) {
		atomic_set(&s_plane_lock, 0);
		atomic_set(&s_panel_busy, 0);
		return STATUS_INVALID_STATE;
	}
	st = epd_refresh_step("erase", epd_display_clear_screen, EPD_COLOR_WHITE);
	if (st != STATUS_OK) {
		epd_busy_fallback_fixed();
		if (!epd_job_prepare(true)) {
			npm1300_board_charge_inhibit_set(false);
			atomic_set(&s_plane_lock, 0);
			atomic_set(&s_panel_busy, 0);
			return STATUS_INVALID_STATE;
		}
	}

	epd_display_demo_bwr_fill(&s_disp, s_demo_title,
				  "4.2in",
				  (s_panel != NULL) ? s_panel->name : "EPD");
	epd_display_fill_rect(&s_disp, 0U, 44U, (uint16_t)(epd_display_width(&s_disp) - 1U), 67U,
			      EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, 4U, 48U, "ink_panel", EPD_FONT_16, EPD_COLOR_BLACK);

	st = epd_refresh_step("demo", epd_display_refresh, EPD_COLOR_WHITE);
	if (st != STATUS_OK) {
		epd_busy_fallback_fixed();
		if (!epd_job_prepare(true)) {
			npm1300_board_charge_inhibit_set(false);
			atomic_set(&s_plane_lock, 0);
			atomic_set(&s_panel_busy, 0);
			return STATUS_INVALID_STATE;
		}
		st = epd_refresh_step("demo_retry", epd_display_refresh, EPD_COLOR_WHITE);
	}
	(void)epd_sleep(&s_disp.epd);
	s_panel_asleep = true;
	npm1300_board_charge_inhibit_set(false);
	atomic_set(&s_plane_lock, 0);
	atomic_set(&s_panel_busy, 0);
	return st;
}

status_t epd_board_lock_planes(void)
{
	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_cas(&s_plane_lock, 0, 1) == false) {
		return STATUS_INVALID_STATE;
	}
	return STATUS_OK;
}

void epd_board_unlock_planes(void)
{
	atomic_set(&s_plane_lock, 0);
}

/**
 * Activate 后应很快 BUSY=1 并维持数秒。轨掉或从未 BUSY → 假刷新。
 * 等待中若 LDO2 掉电则立刻重开并判失败（需整次重踢）。
 */
static status_t epd_board_wait_refresh(int64_t *dt_ms)
{
	int64_t t0 = k_uptime_get();
	uint32_t waited = 0U;
	bool saw_busy = false;

	if (!s_busy_usable) {
		while (waited < 16000U) {
			k_msleep(100);
			waited += 100U;
			if (!npm1300_board_epd_rail_ok()) {
				npm1300_board_epd_power_set(true);
				*dt_ms = k_uptime_get() - t0;
				return STATUS_FAIL;
			}
		}
		*dt_ms = k_uptime_get() - t0;
		return npm1300_board_epd_rail_ok() ? STATUS_OK : STATUS_FAIL;
	}

	k_msleep(20);
	waited = 20U;
	/* Activate 后短窗内必须看到 BUSY，否则多半是掉电假 kick */
	while ((waited < 300U) && (epd_busy() == 0)) {
		if (!npm1300_board_epd_rail_ok()) {
			npm1300_board_epd_power_set(true);
			*dt_ms = k_uptime_get() - t0;
			return STATUS_FAIL;
		}
		k_msleep(20);
		waited += 20U;
	}
	if (epd_busy() == 0) {
		*dt_ms = k_uptime_get() - t0;
		return STATUS_FAIL;
	}
	saw_busy = true;

	while (epd_busy() != 0) {
		if (!npm1300_board_epd_rail_ok()) {
			npm1300_board_epd_power_set(true);
			*dt_ms = k_uptime_get() - t0;
			return STATUS_FAIL;
		}
		k_msleep(50);
		waited += 50U;
		if (waited >= 40000U) {
			*dt_ms = k_uptime_get() - t0;
			return STATUS_FAIL;
		}
	}
	k_msleep(200);
	*dt_ms = k_uptime_get() - t0;

	if (!npm1300_board_epd_rail_ok()) {
		npm1300_board_epd_power_set(true);
		return STATUS_FAIL;
	}
	if (saw_busy && (*dt_ms < (int64_t)EPD_REFRESH_MIN_MS)) {
		return STATUS_FAIL;
	}
	return STATUS_OK;
}

/**
 * @param store_mode/page 非 0xFF：成功刷完后再写 Flash（避免与全刷抢电流）
 */
static status_t epd_board_refresh_core(uint8_t store_mode, uint8_t store_page)
{
	status_t st = STATUS_FAIL;
	epd_display_status_t dst;
	int64_t dt_wait = 0;
	bool force_full;
	bool do_store;
	int pass;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_rails_suspended) != 0) {
		return STATUS_INVALID_STATE;
	}

	atomic_set(&s_panel_busy, 1);

	if (atomic_get(&s_plane_lock) == 0) {
		if (atomic_cas(&s_plane_lock, 0, 1) == false) {
			atomic_set(&s_panel_busy, 0);
			return STATUS_INVALID_STATE;
		}
	}

	force_full = epd_want_full_prepare();
	do_store = (store_mode != 0xFFU);

	/* γ：触摸 LDO1 常开（关轨会导致 CAP1203 I2C NACK 刷屏） */

	for (pass = 0; pass < 2; pass++) {
		bool use_full = force_full || (pass > 0);

		if (!epd_job_prepare(use_full)) {
			st = STATUS_INVALID_STATE;
			break;
		}
		dst = epd_display_refresh_start(&s_disp);

		if (dst != EPD_DISPLAY_OK) {
			st = STATUS_FAIL;
			continue;
		}
		if (!npm1300_board_epd_rail_ok()) {
			npm1300_board_epd_power_set(true);
			st = STATUS_FAIL;
			continue;
		}

		st = epd_board_wait_refresh(&dt_wait);
		if (st == STATUS_OK) {
			break;
		}
		printk("epd: refresh fail pass=%d — retry\n", pass);
	}

	if (st == STATUS_OK) {
#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
		if (do_store && page_slot_is_ready()) {
			status_t sst =
				page_slot_store(store_mode, store_page, s_bw, s_red, s_plane_size);

			if (sst != STATUS_OK) {
				printk("epd: store fail st=%d\n", (int)sst);
			}
		}
#else
		ARG_UNUSED(store_page);
#endif
		if (force_full) {
			s_fast_since_full = 0U;
		} else {
			s_fast_since_full++;
		}
	} else {
		printk("epd: refresh failed\n");
	}

	atomic_set(&s_plane_lock, 0);

	if (k_msgq_num_used_get(&s_epd_jobs) > 0) {
		s_panel_asleep = false;
	} else {
		(void)epd_sleep(&s_disp.epd);
		s_panel_asleep = true;
	}

	npm1300_board_charge_inhibit_set(false);
	atomic_set(&s_panel_busy, 0);
	return st;
}

status_t epd_board_refresh(void)
{
	return epd_board_refresh_core(0xFFU, 0xFFU);
}

status_t epd_board_demo_request(void)
{
	epd_job_t job = {
		.type = EPD_JOB_DEMO,
	};

	return epd_board_queue_job(&job);
}

status_t epd_board_refresh_request(void)
{
	epd_job_t job = {
		.type = EPD_JOB_HOST,
	};

	return epd_board_queue_job(&job);
}

status_t epd_board_store_refresh_request(uint8_t mode, uint8_t page, bool show)
{
	epd_job_t job = {
		.type = show ? EPD_JOB_STORE_REFRESH : EPD_JOB_STORE_ONLY,
		.mode = mode,
		.page = page,
		.show = show ? 1U : 0U,
	};

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	if (!page_slot_mode_page_ok(mode, page)) {
		return STATUS_INVALID_ARG;
	}
#else
	ARG_UNUSED(mode);
	ARG_UNUSED(page);
	ARG_UNUSED(show);
	return STATUS_NOT_SUPPORTED;
#endif
	return epd_board_queue_job(&job);
}

status_t epd_board_show_slot_request(uint8_t mode, uint8_t page)
{
	epd_job_t job = {
		.type = EPD_JOB_SHOW_SLOT,
		.mode = mode,
		.page = page,
	};

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	if (!page_slot_mode_page_ok(mode, page)) {
		return STATUS_INVALID_ARG;
	}
#else
	ARG_UNUSED(mode);
	ARG_UNUSED(page);
	return STATUS_NOT_SUPPORTED;
#endif
	return epd_board_queue_job(&job);
}

uint8_t epd_board_size(void)
{
	return s_size;
}

const char *epd_board_panel_name(void)
{
	return (s_panel != NULL) ? s_panel->name : "?";
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
	return s_plane_size;
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
		memset(s_bw, 0xFF, s_plane_size);
	}
	if ((planes_mask & EPD_BOARD_PLANES_RED) != 0U) {
		memset(s_red, 0x00, s_plane_size);
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
	if (((uint32_t)offset + (uint32_t)len) > s_plane_size) {
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

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
/**
 * 首页叠层画到当前帧缓冲（不落 Flash）。
 * 左下纵向排列；无黑底；字号 12。字库未接中文用 ASCII。
 */
static void epd_board_draw_string_halo(uint16_t x, uint16_t y, const char *str,
				       epd_color_t fg)
{
	epd_display_draw_string(&s_disp, (uint16_t)(x - 1U), y, str, EPD_FONT_12,
				EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, (uint16_t)(x + 1U), y, str, EPD_FONT_12,
				EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, x, (uint16_t)(y - 1U), str, EPD_FONT_12,
				EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, x, (uint16_t)(y + 1U), str, EPD_FONT_12,
				EPD_COLOR_WHITE);
	epd_display_draw_string(&s_disp, x, y, str, EPD_FONT_12, fg);
}

static void epd_board_apply_home_overlay(void)
{
	uint8_t flags;
	char line[32];
	uint8_t notes;
	uint8_t info;
	uint16_t y;
	uint16_t h;
	uint16_t line_h = 14U;
	uint8_t lines = 0U;

	if (!page_slot_is_ready() || !epd_display_is_ready(&s_disp)) {
		return;
	}
	flags = page_slot_home_overlay_get();
	if (flags == 0U) {
		return;
	}

	if ((flags & PAGE_SLOT_OVL_PAGES) != 0U) {
		lines = (uint8_t)(lines + 2U); /* NOTES + INFO */
	}
	if ((flags & PAGE_SLOT_OVL_BATT) != 0U) {
		lines++;
	}
	if (lines == 0U) {
		return;
	}

	h = epd_display_height(&s_disp);
	/* 左下纵向：从下往上留出行距 */
	y = (h > (uint16_t)(lines * line_h + 4U))
		    ? (uint16_t)(h - (uint16_t)(lines * line_h) - 4U)
		    : 2U;

	if ((flags & PAGE_SLOT_OVL_PAGES) != 0U) {
		notes = page_slot_group_content_count(PAGE_SLOT_MODE_NOTES);
		info = page_slot_group_content_count(PAGE_SLOT_MODE_INFO);
		snprintk(line, sizeof(line), "NOTES %uP", (unsigned)notes);
		epd_board_draw_string_halo(6U, y, line, EPD_COLOR_BLACK);
		y = (uint16_t)(y + line_h);
		snprintk(line, sizeof(line), "INFO %uP", (unsigned)info);
		epd_board_draw_string_halo(6U, y, line, EPD_COLOR_BLACK);
		y = (uint16_t)(y + line_h);
	}
	if ((flags & PAGE_SLOT_OVL_BATT) != 0U) {
		uint16_t mv = 0U;
		uint8_t pct = 0U;
		bool charging = false;
		bool full = false;

		if (npm1300_board_batt_get(&mv, &pct, &charging, &full)) {
			if (full) {
				snprintk(line, sizeof(line), "BAT %u%% F",
					 (unsigned)pct);
			} else if (charging) {
				snprintk(line, sizeof(line), "BAT %u%%+",
					 (unsigned)pct);
			} else {
				snprintk(line, sizeof(line), "BAT %u%%",
					 (unsigned)pct);
			}
		} else {
			snprintk(line, sizeof(line), "BAT --");
		}
		epd_board_draw_string_halo(6U, y, line, EPD_COLOR_RED);
	}

	printk("epd: home overlay flags=0x%02x notes=%u info=%u\n", (unsigned)flags,
	       (unsigned)page_slot_group_content_count(PAGE_SLOT_MODE_NOTES),
	       (unsigned)page_slot_group_content_count(PAGE_SLOT_MODE_INFO));
}

static status_t epd_board_run_show_slot(uint8_t mode, uint8_t page)
{
	status_t st;

	if (!page_slot_is_ready()) {
		memset(s_bw, 0xFF, s_plane_size);
		memset(s_red, 0x00, s_plane_size);
	} else {
		st = page_slot_load(mode, page, s_bw, s_red, s_plane_size);
		if (st != STATUS_OK) {
			return st;
		}
	}

	if (mode == PAGE_SLOT_MODE_HOME) {
		epd_board_apply_home_overlay();
	}

	return epd_board_refresh();
}

/**
 * 写槽并刷屏。首页：先存干净底图，再叠层后刷（叠层不写入 Flash）。
 */
static status_t epd_board_run_store_refresh(uint8_t mode, uint8_t page)
{
	status_t st;

	if (mode == PAGE_SLOT_MODE_HOME) {
		st = page_slot_store(mode, page, s_bw, s_red, s_plane_size);
		if (st != STATUS_OK) {
			epd_board_unlock_planes();
			return st;
		}
		epd_board_apply_home_overlay();
		return epd_board_refresh_core(0xFFU, 0xFFU); /* 只刷不存 */
	}

	return epd_board_refresh_core(mode, page);
}

/** 只写 Flash 槽，不刷屏；调用方已持 plane_lock */
static status_t epd_board_run_store_only(uint8_t mode, uint8_t page)
{
	status_t st;

	if (!page_slot_is_ready()) {
		epd_board_unlock_planes();
		return STATUS_INVALID_STATE;
	}

	st = page_slot_store(mode, page, s_bw, s_red, s_plane_size);
	if (st != STATUS_OK) {
		printk("epd: store-only fail st=%d\n", (int)st);
	} else {
		printk("epd: store-only ok mode=%u page=%u\n", (unsigned)mode,
		       (unsigned)page);
	}
	epd_board_unlock_planes();
	return st;
}
#endif

static void epd_worker_thread(void *a, void *b, void *c)
{
	status_t st;
	epd_job_t job;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	st = epd_board_bringup();
	if (st != STATUS_OK) {
		printk("epd: bringup failed: %d\n", (int)st);
		return;
	}

#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
	if (atomic_cas(&s_plane_lock, 0, 1)) {
		st = epd_board_run_show_slot(PAGE_SLOT_MODE_HOME, 0U);
		if (st != STATUS_OK) {
			printk("epd: boot show fail st=%d\n", (int)st);
		}
	}
#else
	st = epd_board_test_run();
	if (st != STATUS_OK) {
		printk("epd: boot demo fail st=%d\n", (int)st);
	}
#endif

	while (1) {
		if (k_msgq_get(&s_epd_jobs, &job, K_FOREVER) != 0) {
			continue;
		}

		if (epd_board_lock_planes() != STATUS_OK) {
			continue;
		}
		if (job.type == EPD_JOB_DEMO) {
			st = epd_board_test_run();
		} else if (job.type == EPD_JOB_HOST) {
			st = epd_board_refresh();
		} else if (job.type == EPD_JOB_SHOW_SLOT) {
#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
			st = epd_board_run_show_slot(job.mode, job.page);
#else
			epd_board_unlock_planes();
			st = STATUS_NOT_SUPPORTED;
#endif
		} else if (job.type == EPD_JOB_STORE_REFRESH) {
#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
			st = epd_board_run_store_refresh(job.mode, job.page);
#else
			epd_board_unlock_planes();
			st = STATUS_NOT_SUPPORTED;
#endif
		} else if (job.type == EPD_JOB_STORE_ONLY) {
#if IS_ENABLED(CONFIG_INK_PANEL_W25Q)
			st = epd_board_run_store_only(job.mode, job.page);
#else
			epd_board_unlock_planes();
			st = STATUS_NOT_SUPPORTED;
#endif
		} else {
			epd_board_unlock_planes();
			continue;
		}

		if (st != STATUS_OK) {
			printk("epd: job fail st=%d type=%u\n", (int)st, (unsigned)job.type);
		}
	}
}

void epd_board_start(void)
{
	if (s_worker_started) {
		return;
	}
	s_worker_started = true;
	s_panel_asleep = true;
	s_fast_since_full = EPD_FULL_EVERY_N;
	atomic_set(&s_plane_lock, 0);
	atomic_set(&s_panel_busy, 0);
	(void)k_msgq_purge(&s_epd_jobs);
	(void)k_thread_create(&s_epd_thread, s_epd_stack, K_THREAD_STACK_SIZEOF(s_epd_stack),
			      epd_worker_thread, NULL, NULL, NULL, 8, 0, K_NO_WAIT);
	k_thread_name_set(&s_epd_thread, "epd");
}
