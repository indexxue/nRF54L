/**
 * @file nfc.c
 * @brief 片上 NFCT 进场检测：FIELD_ON/OFF + DATA_READ + FIELDPRESENT 轮询。
 *
 * 说明：无“磁场强度”ADC。有效性看：
 * - FIELDPRESENT 变化（硬件感应到读卡器场）
 * - FIELD_ON / FIELD_OFF
 * - DATA_READ（手机成功读到 NDEF，对 iPhone 最可靠）
 */

#include "nfc_board.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/drivers/gpio.h>

#include <hal/nrf_nfct.h>
#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>
#include <nrfx_nfct.h>

#include "board.h"
#include "log.h"

#define NDEF_MSG_BUF_SIZE 64U
#define MAX_REC_COUNT     1U
#define NFC_FIELD_LED_IDX 1U /* LED_B */

enum {
	NFC_EVT_FIELD_ON = 1,
	NFC_EVT_FIELD_OFF = 2,
	NFC_EVT_DATA_READ = 3,
};

static atomic_t s_ready;
static atomic_t s_field_on;
static atomic_t s_evt_code;
static atomic_t s_cnt_on;
static atomic_t s_cnt_off;
static atomic_t s_cnt_read;
static struct k_work s_nfc_evt_work;
static uint8_t s_ndef_buf[NDEF_MSG_BUF_SIZE];
static bool s_sense_last;
static bool s_sense_inited;

static const uint8_t s_en_payload[] = {'e', 'i', 'n', 'k', '_', 't', 'a', 'g'};
static const uint8_t s_en_code[] = {'e', 'n'};

static void nfc_field_led_set(bool on)
{
	const board_led_spec_t *led = board_led_spec(NFC_FIELD_LED_IDX);

	if ((led == NULL) || (led->gpio == NULL)) {
		return;
	}
	(void)gpio_pin_set_dt(led->gpio, on ? 1 : 0);
}

static void nfc_evt_work_handler(struct k_work *work)
{
	int code;

	ARG_UNUSED(work);

	code = (int)atomic_get(&s_evt_code);
	atomic_set(&s_evt_code, 0);

	switch (code) {
	case NFC_EVT_FIELD_ON:
		LOG_INFO("nfc: FIELD ON  (cnt_on=%u sense=0x%02x)",
			 (unsigned int)atomic_get(&s_cnt_on),
			 (unsigned int)nfc_board_field_status_raw());
		break;
	case NFC_EVT_FIELD_OFF:
		LOG_INFO("nfc: FIELD OFF (cnt_off=%u sense=0x%02x)",
			 (unsigned int)atomic_get(&s_cnt_off),
			 (unsigned int)nfc_board_field_status_raw());
		break;
	case NFC_EVT_DATA_READ:
		LOG_INFO("nfc: DATA READ ok (cnt_read=%u) — phone read NDEF 'eink_tag'",
			 (unsigned int)atomic_get(&s_cnt_read));
		break;
	default:
		break;
	}
}

static void nfc_queue_evt(int code)
{
	atomic_set(&s_evt_code, code);
	(void)k_work_submit(&s_nfc_evt_work);
}

static void nfc_callback(void *context, nfc_t2t_event_t event, const uint8_t *data,
			 size_t data_length)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);

	switch (event) {
	case NFC_T2T_EVENT_FIELD_ON:
		atomic_set(&s_field_on, 1);
		atomic_inc(&s_cnt_on);
		nfc_field_led_set(true);
		nfc_queue_evt(NFC_EVT_FIELD_ON);
		break;
	case NFC_T2T_EVENT_FIELD_OFF:
		atomic_set(&s_field_on, 0);
		atomic_inc(&s_cnt_off);
		nfc_field_led_set(false);
		nfc_queue_evt(NFC_EVT_FIELD_OFF);
		break;
	case NFC_T2T_EVENT_DATA_READ:
		atomic_inc(&s_cnt_read);
		nfc_queue_evt(NFC_EVT_DATA_READ);
		break;
	default:
		break;
	}
}

static int nfc_payload_prepare(void)
{
	int err;
	uint32_t len = sizeof(s_ndef_buf);

	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_text_rec, UTF_8, s_en_code, sizeof(s_en_code),
				      s_en_payload, sizeof(s_en_payload));
	NFC_NDEF_MSG_DEF(nfc_text_msg, MAX_REC_COUNT);

	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				      &NFC_NDEF_TEXT_RECORD_DESC(nfc_text_rec));
	if (err < 0) {
		return err;
	}

	err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg), s_ndef_buf, &len);
	if (err < 0) {
		return err;
	}

	return nfc_t2t_payload_set(s_ndef_buf, len);
}

status_t nfc_board_init(void)
{
	int err;

	if (!board_wants(BOARD_MASK_NFC)) {
		return STATUS_NOT_SUPPORTED;
	}

	k_work_init(&s_nfc_evt_work, nfc_evt_work_handler);

	err = nfc_t2t_setup(nfc_callback, NULL);
	if (err < 0) {
		LOG_ERROR("nfc: t2t_setup %d", err);
		return STATUS_FAIL;
	}

	err = nfc_payload_prepare();
	if (err < 0) {
		LOG_ERROR("nfc: payload %d", err);
		return STATUS_FAIL;
	}

	err = nfc_t2t_emulation_start();
	if (err < 0) {
		LOG_ERROR("nfc: emulation_start %d", err);
		return STATUS_FAIL;
	}

	atomic_set(&s_ready, 1);
	s_sense_last = nrfx_nfct_field_check();
	s_sense_inited = true;
	LOG_INFO("nfc: ready — iPhone: Control Center NFC Tag Reader, hold on PCB coil");
	LOG_INFO("nfc: watch FIELD/DATA_READ/SENSE; LED_B=in-field (no field-strength ADC)");
	return STATUS_OK;
}

bool nfc_board_is_ready(void)
{
	return atomic_get(&s_ready) != 0;
}

bool nfc_board_field_present(void)
{
	return atomic_get(&s_field_on) != 0;
}

uint8_t nfc_board_field_status_raw(void)
{
	return nrf_nfct_field_status_get(NRF_NFCT);
}

void nfc_board_poll(void)
{
	bool sense;
	uint8_t raw;

	if (!nfc_board_is_ready() || !s_sense_inited) {
		return;
	}

	sense = nrfx_nfct_field_check();
	raw = nfc_board_field_status_raw();

	if (sense != s_sense_last) {
		s_sense_last = sense;
		LOG_INFO("nfc: SENSE %s raw=0x%02x (FIELDPRESENT)", sense ? "ON " : "OFF",
			 (unsigned int)raw);
		nfc_field_led_set(sense);
	}
}

uint32_t nfc_board_count_field_on(void)
{
	return (uint32_t)atomic_get(&s_cnt_on);
}

uint32_t nfc_board_count_field_off(void)
{
	return (uint32_t)atomic_get(&s_cnt_off);
}

uint32_t nfc_board_count_data_read(void)
{
	return (uint32_t)atomic_get(&s_cnt_read);
}
