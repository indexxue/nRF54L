/**
 * @file nfc_board.c
 * @brief ink_panel：NCS T2T + 固定 NDEF Text；回调只 atomic ingress
 */

#include "nfc_board.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#include <nfc_t2t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>

#define NFC_BOARD_MAX_REC       1
#define NFC_BOARD_NDEF_BUF_SIZE 128

static const uint8_t s_en_payload[] = {
	'i', 'n', 'k', '_', 'p', 'a', 'n', 'e', 'l', ' ',
	'N', 'F', 'C', ' ', 'o', 'k'
};
static const uint8_t s_en_code[] = { 'e', 'n' };

static uint8_t s_ndef_msg_buf[NFC_BOARD_NDEF_BUF_SIZE];

static atomic_t s_field_on;
static atomic_t s_field_off;
static atomic_t s_data_read;

static void nfc_board_callback(void *context, nfc_t2t_event_t event,
			       const uint8_t *data, size_t data_length)
{
	ARG_UNUSED(context);
	ARG_UNUSED(data);
	ARG_UNUSED(data_length);

	switch (event) {
	case NFC_T2T_EVENT_FIELD_ON:
		atomic_inc(&s_field_on);
		break;
	case NFC_T2T_EVENT_FIELD_OFF:
		atomic_inc(&s_field_off);
		break;
	case NFC_T2T_EVENT_DATA_READ:
		atomic_inc(&s_data_read);
		break;
	default:
		break;
	}
}

static int nfc_board_encode_payload(uint8_t *buffer, uint32_t *len)
{
	int err;

	NFC_NDEF_TEXT_RECORD_DESC_DEF(nfc_en_text_rec, UTF_8, s_en_code,
				      sizeof(s_en_code), s_en_payload,
				      sizeof(s_en_payload));
	NFC_NDEF_MSG_DEF(nfc_text_msg, NFC_BOARD_MAX_REC);

	err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
				      &NFC_NDEF_TEXT_RECORD_DESC(nfc_en_text_rec));
	if (err < 0) {
		return err;
	}

	return nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg), buffer, len);
}

status_t nfc_board_bringup(void)
{
	int err;
	uint32_t len = sizeof(s_ndef_msg_buf);

	err = nfc_t2t_setup(nfc_board_callback, NULL);
	if (err < 0) {
		printk("nfc: bringup fail err=%d\n", err);
		return STATUS_FAIL;
	}

	err = nfc_board_encode_payload(s_ndef_msg_buf, &len);
	if (err < 0) {
		printk("nfc: bringup fail err=%d\n", err);
		return STATUS_FAIL;
	}

	err = nfc_t2t_payload_set(s_ndef_msg_buf, len);
	if (err < 0) {
		printk("nfc: bringup fail err=%d\n", err);
		return STATUS_FAIL;
	}

	err = nfc_t2t_emulation_start();
	if (err < 0) {
		printk("nfc: bringup fail err=%d\n", err);
		return STATUS_FAIL;
	}

	printk("nfc: bringup ok\n");
	return STATUS_OK;
}

void nfc_board_poll(void)
{
	atomic_val_t n;

	n = atomic_set(&s_field_on, 0);
	if (n != 0) {
		printk("nfc: field on\n");
	}

	n = atomic_set(&s_data_read, 0);
	if (n != 0) {
		printk("nfc: data read\n");
	}

	n = atomic_set(&s_field_off, 0);
	if (n != 0) {
		printk("nfc: field off\n");
	}
}
