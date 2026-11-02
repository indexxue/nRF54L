/**
 * @file pn5180_port.c
 * @brief PN5180 Zephyr port：spi22 + NSS/BUSY/RST（常供电，无 PWR_ON）。
 *
 * 冒烟：pn5180_init → FW。
 * 持续扫卡：专用线程；msgq START/STOP；Inventory 在线程内（可阻塞）。
 */

#include "pn5180_board.h"

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include "board.h"
#include "log.h"
#include "pn5180.h"

#define ZUSER DT_PATH(zephyr_user)

#define PN5180_SPI_BUF_MAX 64U

#define PN5180_SCAN_STACK_SIZE 2048
#define PN5180_SCAN_THREAD_PRIO 9
#define PN5180_SCAN_GAP_MS      80U
#define PN5180_SCAN_RX_TO_MS    80U
#define PN5180_SCAN_UID_LOG_MS  1500U
/** 无卡心跳；也触发详细 diag */
#define PN5180_SCAN_HB_MS       2000U
#define PN5180_SCAN_DIAG_EVERY  5U

#define PN5180_SCAN_CMD_START 1U
#define PN5180_SCAN_CMD_STOP  2U

/** RF_STATUS bit17：TX 驱动开场（NXP RF_STATUS_TX_RF_STATUS） */
#define PN5180_RF_TX_RF_MASK   (1U << 17)
#define PN5180_RF_TS_SHIFT     24U
#define PN5180_RF_TS_MASK      0x7U

static const struct gpio_dt_spec s_busy = GPIO_DT_SPEC_GET(ZUSER, pn5180_busy_gpios);
static const struct gpio_dt_spec s_rst = GPIO_DT_SPEC_GET(ZUSER, pn5180_rst_gpios);

static const board_spi_slave_spec_t *s_slave;
static const board_spi_bus_spec_t *s_bus;
static struct spi_config s_spi_cfg;
static pn5180_t s_dev;
static bool s_ready;

static K_THREAD_STACK_DEFINE(s_scan_stack, PN5180_SCAN_STACK_SIZE);
static struct k_thread s_scan_thread;
K_MSGQ_DEFINE(s_scan_cmdq, sizeof(uint8_t), 2, 1);
static atomic_t s_scan_active;
static bool s_scan_inited;

static const board_spi_slave_spec_t *pn5180_find_slave(void)
{
	const uint8_t n = board_spi_slave_count();

	for (uint8_t i = 0; i < n; i++) {
		const board_spi_slave_spec_t *s = board_spi_slave_spec(i);

		if ((s != NULL) && (s->name != NULL) && (strcmp(s->name, "pn5180") == 0)) {
			return s;
		}
	}
	return NULL;
}

static int pn5180_port_spi_tx_rx(const uint8_t *tx, uint8_t *rx, uint16_t len)
{
	static uint8_t s_dummy_tx[PN5180_SPI_BUF_MAX];
	struct spi_buf tx_buf;
	struct spi_buf rx_buf;
	struct spi_buf_set tx_set;
	struct spi_buf_set rx_set;
	int err;

	if ((s_bus == NULL) || (s_bus->dev == NULL) || (len == 0U)) {
		return -1;
	}
	if (len > PN5180_SPI_BUF_MAX) {
		LOG_ERROR("pn5180: spi len %u > %u", (unsigned)len, (unsigned)PN5180_SPI_BUF_MAX);
		return -1;
	}

	if ((tx != NULL) && (rx != NULL)) {
		tx_buf.buf = (void *)tx;
		tx_buf.len = len;
		rx_buf.buf = rx;
		rx_buf.len = len;
		tx_set.buffers = &tx_buf;
		tx_set.count = 1U;
		rx_set.buffers = &rx_buf;
		rx_set.count = 1U;
		err = spi_transceive(s_bus->dev, &s_spi_cfg, &tx_set, &rx_set);
		return (err == 0) ? 0 : -1;
	}

	if (tx != NULL) {
		tx_buf.buf = (void *)tx;
		tx_buf.len = len;
		tx_set.buffers = &tx_buf;
		tx_set.count = 1U;
		err = spi_write(s_bus->dev, &s_spi_cfg, &tx_set);
		return (err == 0) ? 0 : -1;
	}

	if (rx != NULL) {
		memset(s_dummy_tx, 0xFF, len);
		tx_buf.buf = s_dummy_tx;
		tx_buf.len = len;
		rx_buf.buf = rx;
		rx_buf.len = len;
		tx_set.buffers = &tx_buf;
		tx_set.count = 1U;
		rx_set.buffers = &rx_buf;
		rx_set.count = 1U;
		err = spi_transceive(s_bus->dev, &s_spi_cfg, &tx_set, &rx_set);
		return (err == 0) ? 0 : -1;
	}

	return -1;
}

static void pn5180_port_set_nss(int high)
{
	if ((s_slave == NULL) || (s_slave->cs == NULL)) {
		return;
	}
	(void)gpio_pin_set_raw(s_slave->cs->port, s_slave->cs->pin, high ? 1 : 0);
}

static int pn5180_port_read_busy(void)
{
	return gpio_pin_get_raw(s_busy.port, s_busy.pin);
}

static void pn5180_port_set_rst(int high)
{
	(void)gpio_pin_set_raw(s_rst.port, s_rst.pin, high ? 1 : 0);
}

static void pn5180_port_delay_ms(uint32_t ms)
{
	k_msleep(ms);
}

static status_t pn5180_port_bind(void)
{
	uint32_t bitrate;

	s_slave = pn5180_find_slave();
	if (s_slave == NULL) {
		LOG_ERROR("pn5180: spi slave \"pn5180\" missing in board_config");
		return STATUS_INVALID_STATE;
	}

	s_bus = board_spi_bus_spec(s_slave->bus_index);
	if ((s_bus == NULL) || (s_bus->dev == NULL) || !device_is_ready(s_bus->dev)) {
		LOG_ERROR("pn5180: spi22 not ready");
		return STATUS_FAIL;
	}

	if ((s_slave->cs == NULL) || !gpio_is_ready_dt(s_slave->cs) || !gpio_is_ready_dt(&s_busy) ||
	    !gpio_is_ready_dt(&s_rst)) {
		LOG_ERROR("pn5180: GPIO not ready");
		return STATUS_FAIL;
	}

	if (gpio_pin_configure_dt(s_slave->cs, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_rst, GPIO_OUTPUT_INACTIVE) != 0 ||
	    gpio_pin_configure_dt(&s_busy, GPIO_INPUT) != 0) {
		LOG_ERROR("pn5180: GPIO configure failed");
		return STATUS_FAIL;
	}

	bitrate = s_slave->bitrate_hz;
	if (bitrate == 0U) {
		bitrate = (s_bus->bitrate_hz != 0U) ? s_bus->bitrate_hz : 4000000U;
	}

	memset(&s_spi_cfg, 0, sizeof(s_spi_cfg));
	s_spi_cfg.frequency = bitrate;
	s_spi_cfg.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB;

	LOG_INFO("pn5180: bind spi=%s bitrate=%u CS=P1.14 BUSY=P1.08 RST=P1.07", s_bus->dev->name,
		 (unsigned)bitrate);
	return STATUS_OK;
}

static pn5180_status_t pn5180_port_reenter_transceive(void)
{
	pn5180_status_t st;

	st = pn5180_clear_irq_status(&s_dev, 0xFFFFFFFFu);
	if (st != PN5180_OK) {
		return st;
	}
	st = pn5180_write_register_and_mask(&s_dev, PN5180_REG_SYSTEM_CONFIG, 0xFFFFFFF8u);
	if (st != PN5180_OK) {
		return st;
	}
	return pn5180_write_register_or_mask(&s_dev, PN5180_REG_SYSTEM_CONFIG,
					     PN5180_SYS_CMD_TRANSCEIVE);
}

static void pn5180_port_dump_regs(const char *tag)
{
	uint32_t irq = 0U;
	uint32_t irq_en = 0U;
	uint32_t rx = 0U;
	uint32_t rf = 0U;
	uint32_t sys = 0U;
	uint32_t syscfg = 0U;
	uint32_t crc_rx = 0U;
	uint32_t crc_tx = 0U;

	(void)pn5180_read_register(&s_dev, PN5180_REG_IRQ_STATUS, &irq);
	(void)pn5180_read_register(&s_dev, PN5180_REG_IRQ_ENABLE, &irq_en);
	(void)pn5180_read_register(&s_dev, PN5180_REG_RX_STATUS, &rx);
	(void)pn5180_read_register(&s_dev, PN5180_REG_RF_STATUS, &rf);
	(void)pn5180_read_register(&s_dev, PN5180_REG_SYSTEM_STATUS, &sys);
	(void)pn5180_read_register(&s_dev, PN5180_REG_SYSTEM_CONFIG, &syscfg);
	(void)pn5180_read_register(&s_dev, PN5180_REG_CRC_RX_CONFIG, &crc_rx);
	(void)pn5180_read_register(&s_dev, PN5180_REG_CRC_TX_CONFIG, &crc_tx);

	LOG_INFO("pn5180: DIAG[%s] IRQ=0x%08X IRQ_EN=0x%08X RX_ST=0x%08X(len=%u)",
		 tag, (unsigned)irq, (unsigned)irq_en, (unsigned)rx,
		 (unsigned)(rx & 0x1FFU));
	LOG_INFO("pn5180: DIAG[%s] RF=0x%08X TX_RF=%u TS=%u SYS_ST=0x%08X SYS_CFG=0x%08X",
		 tag, (unsigned)rf, (unsigned)((rf & PN5180_RF_TX_RF_MASK) != 0U),
		 (unsigned)((rf >> PN5180_RF_TS_SHIFT) & PN5180_RF_TS_MASK), (unsigned)sys,
		 (unsigned)syscfg);
	LOG_INFO("pn5180: DIAG[%s] CRC_RX=0x%X CRC_TX=0x%X", tag, (unsigned)crc_rx,
		 (unsigned)crc_tx);
}

/**
 * 发一帧后轮询：IRQ RX/SOF 或 RX_STATUS.len>0（不单靠 SOF）。
 * verbose=true 时失败打寄存器。
 */
static status_t pn5180_port_tx_then_rx(const uint8_t *tx, uint16_t tx_len, uint8_t valid_bits,
				       uint8_t *rx, uint16_t rx_expect, uint32_t *last_irq,
				       bool verbose, const char *step)
{
	pn5180_status_t st;
	uint32_t irq = 0U;
	uint16_t len = 0U;
	uint32_t elapsed = 0U;
	const uint32_t poll_ms = 5U;

	st = pn5180_send_data(&s_dev, tx, tx_len, valid_bits);
	if (st != PN5180_OK) {
		if (verbose) {
			LOG_ERROR("pn5180: DIAG %s SEND_DATA fail st=%d", step, (int)st);
			pn5180_port_dump_regs(step);
		}
		return STATUS_FAIL;
	}

	if (rx_expect == 0U) {
		k_msleep(2);
		(void)pn5180_clear_irq_status(&s_dev, 0xFFFFFFFFu);
		return STATUS_OK;
	}

	while (elapsed < PN5180_SCAN_RX_TO_MS) {
		(void)pn5180_get_irq_status(&s_dev, &irq);
		(void)pn5180_get_rx_length(&s_dev, &len);
		if (last_irq != NULL) {
			*last_irq = irq;
		}
		if (((irq & (PN5180_IRQ_RX_STAT | PN5180_IRQ_RX_SOF_DET_STAT)) != 0U) ||
		    (len >= rx_expect)) {
			break;
		}
		k_msleep(poll_ms);
		elapsed += poll_ms;
	}

	(void)pn5180_get_irq_status(&s_dev, &irq);
	(void)pn5180_get_rx_length(&s_dev, &len);
	if (last_irq != NULL) {
		*last_irq = irq;
	}

	if (len < rx_expect) {
		if (verbose) {
			LOG_WARN("pn5180: DIAG %s no RX (irq=0x%08X rx_len=%u need=%u) → 无卡回波/场弱/协议",
				 step, (unsigned)irq, (unsigned)len, (unsigned)rx_expect);
			pn5180_port_dump_regs(step);
		}
		(void)pn5180_clear_irq_status(&s_dev, 0xFFFFFFFFu);
		return STATUS_TIMEOUT;
	}

	if (rx == NULL) {
		(void)pn5180_clear_irq_status(&s_dev, 0xFFFFFFFFu);
		return STATUS_FAIL;
	}

	st = pn5180_read_data(&s_dev, rx, rx_expect);
	(void)pn5180_clear_irq_status(&s_dev, 0xFFFFFFFFu);
	if (st != PN5180_OK) {
		if (verbose) {
			LOG_ERROR("pn5180: DIAG %s READ_DATA fail st=%d", step, (int)st);
		}
		return STATUS_FAIL;
	}

	if (verbose) {
		if (rx_expect >= 2U) {
			LOG_INFO("pn5180: DIAG %s OK rx[0]=0x%02X rx[1]=0x%02X len=%u", step, rx[0],
				 rx[1], (unsigned)rx_expect);
		} else {
			LOG_INFO("pn5180: DIAG %s OK rx[0]=0x%02X", step, rx[0]);
		}
	}
	return STATUS_OK;
}

static status_t pn5180_port_typea_prep_short_frame(void)
{
	pn5180_status_t st;

	/* 与 Arduino activateTypeA 一致：每轮重载 TypeA RF */
	st = pn5180_load_rf_config(&s_dev, PN5180_RF_TX_ISO14443A, PN5180_RF_RX_ISO14443A);
	if (st != PN5180_OK) {
		return st;
	}

	st = pn5180_write_register_and_mask(&s_dev, PN5180_REG_SYSTEM_CONFIG, 0xFFFFFFBFu);
	if (st != PN5180_OK) {
		return st;
	}
	st = pn5180_write_register_and_mask(&s_dev, PN5180_REG_CRC_RX_CONFIG, 0xFFFFFFFEu);
	if (st != PN5180_OK) {
		return st;
	}
	st = pn5180_write_register_and_mask(&s_dev, PN5180_REG_CRC_TX_CONFIG, 0xFFFFFFFEu);
	if (st != PN5180_OK) {
		return st;
	}

	/* 打开常用 IRQ 源，便于诊断 */
	st = pn5180_write_register(&s_dev, PN5180_REG_IRQ_ENABLE, 0x001FFFFFu);
	if (st != PN5180_OK) {
		return st;
	}

	return pn5180_port_reenter_transceive();
}

/**
 * ISO14443-A：先 WUPA(0x52) 再 REQA(0x26)；NTAG216 → 7 字节 UID。
 */
static status_t pn5180_port_typea_activate(uint8_t uid_out[7], uint8_t *uid_len, uint32_t *last_irq,
					   bool verbose)
{
	uint8_t cmd[7];
	uint8_t atqa[2];
	uint8_t sak = 0U;
	pn5180_status_t st;
	status_t r;
	const uint8_t wake_cmds[2] = {0x52U, 0x26U}; /* WUPA, REQA */
	uint8_t wi;

	if ((uid_out == NULL) || (uid_len == NULL)) {
		return STATUS_INVALID_ARG;
	}
	*uid_len = 0U;

	st = pn5180_port_typea_prep_short_frame();
	if (st != PN5180_OK) {
		if (verbose) {
			LOG_ERROR("pn5180: DIAG prep fail st=%d", (int)st);
			pn5180_port_dump_regs("prep");
		}
		return STATUS_FAIL;
	}

	r = STATUS_TIMEOUT;
	for (wi = 0U; wi < 2U; wi++) {
		cmd[0] = wake_cmds[wi];
		if (verbose) {
			LOG_INFO("pn5180: DIAG try %s 7bit", (wi == 0U) ? "WUPA(0x52)" : "REQA(0x26)");
		}
		r = pn5180_port_tx_then_rx(cmd, 1U, 0x07U, atqa, 2U, last_irq, verbose,
					   (wi == 0U) ? "WUPA" : "REQA");
		if (r == STATUS_OK) {
			break;
		}
		/* 再进 Transceive 试另一种唤醒 */
		st = pn5180_port_typea_prep_short_frame();
		if (st != PN5180_OK) {
			return STATUS_FAIL;
		}
	}
	if (r != STATUS_OK) {
		return r;
	}

	/* Anticollision CL1（仍无 CRC） */
	cmd[0] = 0x93U;
	cmd[1] = 0x20U;
	r = pn5180_port_tx_then_rx(cmd, 2U, 0U, &cmd[2], 5U, last_irq, verbose, "AC_CL1");
	if (r != STATUS_OK) {
		return r;
	}

	st = pn5180_write_register_or_mask(&s_dev, PN5180_REG_CRC_RX_CONFIG, 0x01U);
	if (st != PN5180_OK) {
		return STATUS_FAIL;
	}
	st = pn5180_write_register_or_mask(&s_dev, PN5180_REG_CRC_TX_CONFIG, 0x01U);
	if (st != PN5180_OK) {
		return STATUS_FAIL;
	}
	cmd[0] = 0x93U;
	cmd[1] = 0x70U;
	r = pn5180_port_tx_then_rx(cmd, 7U, 0U, &sak, 1U, last_irq, verbose, "SEL_CL1");
	if (r != STATUS_OK) {
		return r;
	}

	if ((sak & 0x04U) == 0U) {
		memcpy(uid_out, &cmd[2], 4U);
		memset(&uid_out[4], 0, 3U);
		*uid_len = 4U;
		return STATUS_OK;
	}

	if (cmd[2] != 0x88U) {
		if (verbose) {
			LOG_ERROR("pn5180: DIAG cascade CT=0x%02X (expect 0x88)", cmd[2]);
		}
		return STATUS_FAIL;
	}
	memcpy(uid_out, &cmd[3], 3U);

	st = pn5180_write_register_and_mask(&s_dev, PN5180_REG_CRC_RX_CONFIG, 0xFFFFFFFEu);
	if (st != PN5180_OK) {
		return STATUS_FAIL;
	}
	st = pn5180_write_register_and_mask(&s_dev, PN5180_REG_CRC_TX_CONFIG, 0xFFFFFFFEu);
	if (st != PN5180_OK) {
		return STATUS_FAIL;
	}

	cmd[0] = 0x95U;
	cmd[1] = 0x20U;
	r = pn5180_port_tx_then_rx(cmd, 2U, 0U, &cmd[2], 5U, last_irq, verbose, "AC_CL2");
	if (r != STATUS_OK) {
		return r;
	}
	memcpy(&uid_out[3], &cmd[2], 4U);

	st = pn5180_write_register_or_mask(&s_dev, PN5180_REG_CRC_RX_CONFIG, 0x01U);
	if (st != PN5180_OK) {
		return STATUS_FAIL;
	}
	st = pn5180_write_register_or_mask(&s_dev, PN5180_REG_CRC_TX_CONFIG, 0x01U);
	if (st != PN5180_OK) {
		return STATUS_FAIL;
	}
	cmd[0] = 0x95U;
	cmd[1] = 0x70U;
	r = pn5180_port_tx_then_rx(cmd, 7U, 0U, &sak, 1U, last_irq, verbose, "SEL_CL2");
	if (r != STATUS_OK) {
		return r;
	}

	*uid_len = 7U;
	return STATUS_OK;
}

static void pn5180_port_typea_halt(void)
{
	uint8_t cmd[2] = {0x50U, 0x00U};

	(void)pn5180_write_register_or_mask(&s_dev, PN5180_REG_CRC_RX_CONFIG, 0x01U);
	(void)pn5180_write_register_or_mask(&s_dev, PN5180_REG_CRC_TX_CONFIG, 0x01U);
	(void)pn5180_port_reenter_transceive();
	(void)pn5180_port_tx_then_rx(cmd, 2U, 0U, NULL, 0U, NULL, false, "HALT");
}

/** 开扫时一次：判断软件链路 vs 无回波 */
static void pn5180_port_rf_self_judge(void)
{
	uint32_t rf = 0U;
	uint32_t irq = 0U;
	uint8_t uid[7];
	uint8_t uid_len = 0U;
	status_t st;
	uint8_t fw[2];
	uint8_t die[8];

	LOG_INFO("pn5180: ==== RF self-judge begin (place NTAG on antenna) ====");

	if (pn5180_read_firmware_version(&s_dev, fw) == PN5180_OK) {
		LOG_INFO("pn5180: JUDGE fw=0x%02X 0x%02X (SPI host OK)", fw[0], fw[1]);
	} else {
		LOG_ERROR("pn5180: JUDGE fw read FAIL → SPI/软件主机口问题");
		return;
	}

	if (pn5180_read_eeprom(&s_dev, PN5180_EEPROM_DIE_IDENTIFIER, die, 8U) == PN5180_OK) {
		LOG_INFO("pn5180: JUDGE die=%02X%02X%02X%02X…", die[0], die[1], die[2], die[3]);
	}

	pn5180_port_dump_regs("after_rf_on");

	(void)pn5180_read_register(&s_dev, PN5180_REG_RF_STATUS, &rf);
	{
		unsigned tx_rf = (unsigned)((rf & PN5180_RF_TX_RF_MASK) != 0U);
		unsigned ts = (unsigned)((rf >> PN5180_RF_TS_SHIFT) & PN5180_RF_TS_MASK);

		LOG_INFO("pn5180: JUDGE RF=0x%08X TX_RF=%u TS=%u (TX_RF=1 才算芯片开场)",
			 (unsigned)rf, tx_rf, ts);
		if (tx_rf == 0U) {
			LOG_WARN("pn5180: JUDGE TX_RF=0 → 先查 rf_on/TVDD/驱动，勿先怪天线匹配");
		}
	}

	st = pn5180_port_typea_activate(uid, &uid_len, &irq, true);
	if (st == STATUS_OK) {
		LOG_INFO("pn5180: JUDGE PASS TypeA uid_len=%u — 软件+天线路径 OK",
			 (unsigned)uid_len);
		pn5180_port_typea_halt();
	} else if (st == STATUS_TIMEOUT) {
		(void)pn5180_read_register(&s_dev, PN5180_REG_RF_STATUS, &rf);
		LOG_WARN("pn5180: JUDGE: SPI/寄存器 OK，但 REQA/WUPA 无 RX");
		LOG_WARN("pn5180: JUDGE after_fail RF=0x%08X TX_RF=%u TS=%u irq=0x%08X",
			 (unsigned)rf, (unsigned)((rf & PN5180_RF_TX_RF_MASK) != 0U),
			 (unsigned)((rf >> PN5180_RF_TS_SHIFT) & PN5180_RF_TS_MASK),
			 (unsigned)irq);
		if ((rf & PN5180_RF_TX_RF_MASK) != 0U) {
			LOG_WARN("pn5180: JUDGE → TX_RF=1 无回波：倾向天线匹配/贴卡/TVDD 峰值/线圈");
		} else {
			LOG_WARN("pn5180: JUDGE → TX_RF=0 无回波：优先 TVDD/开场，再查协议");
		}
	} else {
		LOG_ERROR("pn5180: JUDGE FAIL st=%d — 软件配置/总线错误", (int)st);
	}

	LOG_INFO("pn5180: ==== RF self-judge end ====");
}

static bool pn5180_scan_cmd_is_stop(void)
{
	uint8_t cmd;

	while (k_msgq_get(&s_scan_cmdq, &cmd, K_NO_WAIT) == 0) {
		if (cmd == PN5180_SCAN_CMD_STOP) {
			return true;
		}
	}
	return false;
}

static void pn5180_scan_session(void)
{
	pn5180_status_t st;
	uint8_t uid[7];
	uint8_t last_uid[7];
	uint8_t uid_len = 0U;
	bool have_last = false;
	int64_t last_log_ms = 0;
	int64_t last_hb_ms = 0;
	uint32_t miss = 0U;
	uint32_t last_irq = 0U;
	uint32_t diag_n = 0U;
	status_t inv_st;

	LOG_INFO("pn5180: scan START (ISO14443-A / NTAG216)");
	atomic_set(&s_scan_active, 1);

	st = pn5180_setup_rf(&s_dev, PN5180_RF_TX_ISO14443A, PN5180_RF_RX_ISO14443A);
	if (st != PN5180_OK) {
		LOG_ERROR("pn5180: setup_rf TypeA failed st=%d → 软件/芯片 RF_ON", (int)st);
		atomic_set(&s_scan_active, 0);
		return;
	}

	/* 开场后等稳定再判 */
	k_msleep(5);
	pn5180_port_rf_self_judge();

	last_hb_ms = k_uptime_get();

	while (!pn5180_scan_cmd_is_stop()) {
		bool verbose = false;

		if ((miss > 0U) && ((miss % PN5180_SCAN_DIAG_EVERY) == 0U)) {
			verbose = true;
			diag_n++;
		}

		inv_st = pn5180_port_typea_activate(uid, &uid_len, &last_irq, verbose);
		if (inv_st == STATUS_OK) {
			int64_t now = k_uptime_get();
			bool same = have_last && (uid_len > 0U) && (memcmp(uid, last_uid, uid_len) == 0);
			bool due = (!same) || ((now - last_log_ms) >= (int64_t)PN5180_SCAN_UID_LOG_MS);

			miss = 0U;
			if (due) {
				if (uid_len >= 7U) {
					LOG_INFO("pn5180: TypeA uid7=%02X:%02X:%02X:%02X:%02X:%02X:%02X",
						 uid[0], uid[1], uid[2], uid[3], uid[4], uid[5],
						 uid[6]);
				} else {
					LOG_INFO("pn5180: TypeA uid4=%02X:%02X:%02X:%02X", uid[0],
						 uid[1], uid[2], uid[3]);
				}
				memcpy(last_uid, uid, sizeof(last_uid));
				have_last = true;
				last_log_ms = now;
			}
			pn5180_port_typea_halt();
		} else if (inv_st == STATUS_FAIL) {
			LOG_WARN("pn5180: TypeA activate error");
			miss++;
		} else {
			miss++;
		}

		{
			int64_t now = k_uptime_get();

			if ((now - last_hb_ms) >= (int64_t)PN5180_SCAN_HB_MS) {
				LOG_INFO("pn5180: scanning TypeA… miss=%u irq=0x%08X diag=%u",
					 (unsigned)miss, (unsigned)last_irq, (unsigned)diag_n);
				last_hb_ms = now;
			}
		}

		if (pn5180_scan_cmd_is_stop()) {
			break;
		}
		k_msleep(PN5180_SCAN_GAP_MS);
	}

	st = pn5180_rf_off(&s_dev);
	if (st != PN5180_OK) {
		LOG_ERROR("pn5180: rf_off st=%d", (int)st);
	}

	atomic_set(&s_scan_active, 0);
	LOG_INFO("pn5180: scan STOP");
}

static void pn5180_scan_worker(void *p1, void *p2, void *p3)
{
	uint8_t cmd;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	LOG_INFO("pn5180: scan worker ready");

	while (1) {
		if (k_msgq_get(&s_scan_cmdq, &cmd, K_FOREVER) != 0) {
			continue;
		}
		if (cmd != PN5180_SCAN_CMD_START) {
			continue;
		}
		pn5180_scan_session();
	}
}

status_t pn5180_board_smoke(void)
{
	pn5180_config_t cfg = PN5180_CONFIG_DEFAULT;
	pn5180_status_t st;
	uint8_t fw[2];
	status_t bind_st;

	s_ready = false;

	bind_st = pn5180_port_bind();
	if (bind_st != STATUS_OK) {
		return bind_st;
	}

	cfg.spi_tx_rx = pn5180_port_spi_tx_rx;
	cfg.set_nss = pn5180_port_set_nss;
	cfg.read_busy = pn5180_port_read_busy;
	cfg.set_rst = pn5180_port_set_rst;
	cfg.set_pwr_on = NULL;
	cfg.delay_ms = pn5180_port_delay_ms;
	cfg.busy_timeout_ms = 1000U;
	cfg.busy_active_high = true;
	cfg.reset_on_init = true;

	st = pn5180_register(&s_dev, &cfg);
	if (st != PN5180_OK) {
		LOG_ERROR("pn5180: register failed st=%d", (int)st);
		return STATUS_FAIL;
	}

	LOG_INFO("pn5180: init begin (RST + IDLE + FW)");
	st = pn5180_init(&s_dev);
	if (st != PN5180_OK) {
		LOG_ERROR("pn5180: init failed st=%d (BUS/BUSY/ID?)", (int)st);
		return STATUS_FAIL;
	}

	st = pn5180_read_firmware_version(&s_dev, fw);
	if (st != PN5180_OK) {
		LOG_ERROR("pn5180: read FW failed st=%d", (int)st);
		return STATUS_FAIL;
	}

	s_ready = true;
	LOG_INFO("pn5180: ok fw=0x%02X 0x%02X (SPI link up)", fw[0], fw[1]);
	return STATUS_OK;
}

status_t pn5180_board_scan_init(void)
{
	k_tid_t tid;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (s_scan_inited) {
		return STATUS_OK;
	}

	atomic_set(&s_scan_active, 0);
	tid = k_thread_create(&s_scan_thread, s_scan_stack, K_THREAD_STACK_SIZEOF(s_scan_stack),
			      pn5180_scan_worker, NULL, NULL, NULL,
			      K_PRIO_PREEMPT(PN5180_SCAN_THREAD_PRIO), 0, K_NO_WAIT);
	if (tid == NULL) {
		LOG_ERROR("pn5180: scan thread create failed");
		return STATUS_FAIL;
	}

	k_thread_name_set(tid, "pn5180_scan");
	s_scan_inited = true;
	LOG_INFO("pn5180: scan thread created (BTN toggle start/stop)");
	return STATUS_OK;
}

status_t pn5180_board_scan_start(void)
{
	uint8_t cmd = PN5180_SCAN_CMD_START;

	if (!s_ready || !s_scan_inited) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_scan_active) != 0) {
		return STATUS_INVALID_STATE;
	}
	if (k_msgq_put(&s_scan_cmdq, &cmd, K_NO_WAIT) != 0) {
		LOG_WARN("pn5180: scan cmdq full (start)");
		return STATUS_FAIL;
	}
	return STATUS_OK;
}

status_t pn5180_board_scan_stop(void)
{
	uint8_t cmd = PN5180_SCAN_CMD_STOP;

	if (!s_ready || !s_scan_inited) {
		return STATUS_INVALID_STATE;
	}
	if (atomic_get(&s_scan_active) == 0) {
		return STATUS_INVALID_STATE;
	}
	if (k_msgq_put(&s_scan_cmdq, &cmd, K_NO_WAIT) != 0) {
		LOG_WARN("pn5180: scan cmdq full (stop)");
		return STATUS_FAIL;
	}
	return STATUS_OK;
}

bool pn5180_board_is_ready(void)
{
	return s_ready;
}

bool pn5180_board_scan_is_active(void)
{
	return atomic_get(&s_scan_active) != 0;
}
