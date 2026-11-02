/**
 * @file cmd.c
 * @brief Factory command framework (Zephyr). Lives under projects/factory/lib/cmd/.
 *
 * S0 (plan_zigbee_factory_temp_coord): local_only + remote capture path for zbcmd.
 */

#include "cmd.h"

#include "button.h"
#include "fw_version.h"
#include "led_scene.h"
#include "log.h"
#include "nvs.h"
#include "zbft.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/console/console.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

#include <nrfx.h>
#include <nrfx_rramc.h>

/* Zigbee-linked factory: snprintk + console need more than 1 KiB. */
#define CMD_RX_STACK_SIZE 2048
#define CMD_RX_PRIORITY   5

static cmd_write_fn s_write_fn;
static void *s_write_ctx;
static cmd_entry_t s_cmd_table[CMD_MAX_COMMANDS];
static int s_cmd_count;
static struct k_mutex s_uart_mutex;
static bool s_uart_mutex_ready;
static void (*s_project_register_fn)(void);
static bool s_project_register_done;

static char s_line_buf[CMD_LINE_MAX];
static size_t s_line_len;

static char *s_cap_buf;
static size_t s_cap_sz;
static size_t s_cap_len;
static bool s_cap_active;
/* process_line holds uart mutex; replies must use unlocked send */
static bool s_cmd_tx_nested;

static K_THREAD_STACK_DEFINE(s_cmd_rx_stack, CMD_RX_STACK_SIZE);
static struct k_thread s_cmd_rx_thread;
static bool s_cmd_rx_started;

static bool cmd_write_printk(const void *data, size_t len, void *user_ctx)
{
	const char *p = (const char *)data;

	(void)user_ctx;
	if ((data == NULL) || (len == 0U)) {
		return true;
	}

	for (size_t i = 0U; i < len; i++) {
		printk("%c", p[i]);
	}

	return true;
}

static void cmd_cap_append(const uint8_t *data, uint16_t len)
{
	size_t room;
	size_t n;

	if (!s_cap_active || (s_cap_buf == NULL) || (s_cap_sz == 0U) || (data == NULL) ||
	    (len == 0U)) {
		return;
	}

	/* Reserve 1 byte for NUL */
	if (s_cap_sz <= 1U) {
		return;
	}

	room = (s_cap_sz - 1U) - s_cap_len;
	n = ((size_t)len < room) ? (size_t)len : room;
	if (n == 0U) {
		return;
	}

	(void)memcpy(&s_cap_buf[s_cap_len], data, n);
	s_cap_len += n;
	s_cap_buf[s_cap_len] = '\0';
}

void cmd_init(cmd_write_fn write_fn, void *write_ctx)
{
	if (!s_uart_mutex_ready) {
		(void)k_mutex_init(&s_uart_mutex);
		s_uart_mutex_ready = true;
	}

	s_write_fn = (write_fn != NULL) ? write_fn : cmd_write_printk;
	s_write_ctx = write_ctx;
	s_cmd_count = 0;
	(void)memset(s_cmd_table, 0, sizeof(s_cmd_table));
	s_line_len = 0U;
	s_cap_active = false;
	s_cap_buf = NULL;
	s_cap_sz = 0U;
	s_cap_len = 0U;
	s_cmd_tx_nested = false;
}

static int cmd_register_ex(const char *name, cmd_handler_t handler, const char *help,
			   bool local_only)
{
	size_t nlen;

	if ((name == NULL) || (handler == NULL) || (s_cmd_count >= (int)CMD_MAX_COMMANDS)) {
		return -1;
	}

	nlen = strlen(name);
	if ((nlen == 0U) || (nlen >= CMD_NAME_MAX)) {
		return -1;
	}

	(void)strncpy(s_cmd_table[s_cmd_count].name, name, CMD_NAME_MAX - 1U);
	s_cmd_table[s_cmd_count].name[CMD_NAME_MAX - 1U] = '\0';
	s_cmd_table[s_cmd_count].handler = handler;
	s_cmd_table[s_cmd_count].local_only = local_only;

	if (help != NULL) {
		(void)strncpy(s_cmd_table[s_cmd_count].help, help, CMD_HELP_MAX - 1U);
		s_cmd_table[s_cmd_count].help[CMD_HELP_MAX - 1U] = '\0';
	} else {
		s_cmd_table[s_cmd_count].help[0] = '\0';
	}

	s_cmd_count++;
	return 0;
}

int cmd_register(const char *name, cmd_handler_t handler, const char *help)
{
	return cmd_register_ex(name, handler, help, false);
}

int cmd_register_local(const char *name, cmd_handler_t handler, const char *help)
{
	return cmd_register_ex(name, handler, help, true);
}

bool cmd_is_local_only(const char *name)
{
	if ((name == NULL) || (name[0] == '\0')) {
		return true;
	}

	for (int i = 0; i < s_cmd_count; i++) {
		if (strcmp(name, s_cmd_table[i].name) == 0) {
			return s_cmd_table[i].local_only;
		}
	}

	/* Unknown: treat as not air-safe */
	return true;
}

static bool cmd_send_unlocked(const uint8_t *data, uint16_t len)
{
	bool ok = true;

	if ((data == NULL) || (len == 0U)) {
		return false;
	}

	cmd_cap_append(data, len);

	if (s_write_fn == NULL) {
		return s_cap_active;
	}

	ok = s_write_fn(data, (size_t)len, s_write_ctx);
	return ok;
}

bool cmd_send(const uint8_t *data, uint16_t len)
{
	bool ok;

	if ((data == NULL) || (len == 0U)) {
		return false;
	}

	if (s_cmd_tx_nested) {
		return cmd_send_unlocked(data, len);
	}

	if (s_uart_mutex_ready) {
		if (k_mutex_lock(&s_uart_mutex, K_MSEC(CMD_TX_MUTEX_TIMEOUT_MS)) != 0) {
			return false;
		}
	}

	ok = cmd_send_unlocked(data, len);

	if (s_uart_mutex_ready) {
		(void)k_mutex_unlock(&s_uart_mutex);
	}

	return ok;
}

void cmd_uart_lock(void)
{
	if (s_uart_mutex_ready) {
		(void)k_mutex_lock(&s_uart_mutex, K_FOREVER);
	}
}

void cmd_uart_unlock(void)
{
	if (s_uart_mutex_ready) {
		(void)k_mutex_unlock(&s_uart_mutex);
	}
}

bool cmd_send_str(const char *str)
{
	size_t len;

	if (str == NULL) {
		return false;
	}

	len = strlen(str);
	if (len == 0U) {
		return true;
	}

	if (len > 0xFFFFU) {
		return false;
	}

	return cmd_send((const uint8_t *)str, (uint16_t)len);
}

void cmd_reply_ok(const char *cmd, const char *value)
{
	char buf[CMD_LINE_MAX + 16U];
	int n;

	n = snprintk(buf, sizeof(buf), "%s:%s\r\n", (cmd != NULL) ? cmd : "",
		     (value != NULL) ? value : "");
	if ((n > 0) && ((size_t)n < sizeof(buf))) {
		(void)cmd_send((const uint8_t *)buf, (uint16_t)n);
	}
}

void cmd_reply_ng(void)
{
	(void)cmd_send_str("ng\r\n");
}

static void trim_and_tokenize(char *line, const char *argv[], int *argc, int max_argc)
{
	*argc = 0;
	while ((*line == ' ') || (*line == '\t')) {
		line++;
	}

	while ((*line != '\0') && (*argc < max_argc)) {
		argv[*argc] = line;
		(*argc)++;
		while ((*line != '\0') && (*line != ' ') && (*line != '\t') && (*line != '\r') &&
		       (*line != '\n')) {
			*line = (char)tolower((unsigned char)*line);
			line++;
		}

		if (*line == '\0') {
			break;
		}

		*line = '\0';
		line++;
		while ((*line == ' ') || (*line == '\t')) {
			line++;
		}
	}
}

static void do_help(void)
{
	char buf[CMD_STATUS_BUF_SIZE];
	int n;

	n = snprintk(buf, sizeof(buf), "help: list commands and usage\r\n");
	if ((n > 0) && ((size_t)n < sizeof(buf))) {
		(void)cmd_send((const uint8_t *)buf, (uint16_t)n);
	}

	for (int i = 0; i < s_cmd_count; i++) {
		const char *h = s_cmd_table[i].help;

		if (h[0] == '\0') {
			h = "";
		}

		if (s_cmd_table[i].local_only) {
			n = snprintk(buf, sizeof(buf), "%s: [local] %s\r\n", s_cmd_table[i].name,
				     h);
		} else {
			n = snprintk(buf, sizeof(buf), "%s: %s\r\n", s_cmd_table[i].name, h);
		}

		if ((n > 0) && ((size_t)n < sizeof(buf))) {
			(void)cmd_send((const uint8_t *)buf, (uint16_t)n);
		}
	}
}

/**
 * @param remote_only when true, reject local_only handlers with ng.
 * @return true if help or a handler ran; false if ng path.
 */
static bool cmd_process_line_locked(const char *line, bool remote_only)
{
	char buf[CMD_LINE_MAX];
	size_t len;
	const char *argv[CMD_MAX_ARGC];
	int argc = 0;

	if (line == NULL) {
		cmd_reply_ng();
		return false;
	}

	len = strlen(line);
	if (len >= sizeof(buf)) {
		len = sizeof(buf) - 1U;
	}

	(void)memcpy(buf, line, len);
	buf[len] = '\0';

	for (size_t i = 0U; i < len; i++) {
		if ((buf[i] == '\r') || (buf[i] == '\n')) {
			buf[i] = '\0';
			break;
		}
	}

	{
		char *space_pos = strchr(buf, ' ');
		char *tab_pos = strchr(buf, '\t');
		char *sep = strchr(buf, ':');
		char *first_blank = space_pos;

		if ((first_blank == NULL) || ((tab_pos != NULL) && (tab_pos < first_blank))) {
			first_blank = tab_pos;
		}

		if ((sep != NULL) && ((first_blank == NULL) || (sep < first_blank))) {
			*sep = ' ';
		}
	}

	trim_and_tokenize(buf, argv, &argc, (int)CMD_MAX_ARGC);
	if (argc == 0) {
		cmd_reply_ng();
		return false;
	}

	if (strcmp(argv[0], "help") == 0) {
		do_help();
		return true;
	}

	for (int i = 0; i < s_cmd_count; i++) {
		if (strcmp(argv[0], s_cmd_table[i].name) == 0) {
			if (remote_only && s_cmd_table[i].local_only) {
				cmd_reply_ng();
				return false;
			}
			s_cmd_table[i].handler(argc, argv);
			return true;
		}
	}

	cmd_reply_ng();
	return false;
}

void cmd_process_line(const char *line)
{
	if (s_uart_mutex_ready) {
		(void)k_mutex_lock(&s_uart_mutex, K_FOREVER);
	}

	s_cmd_tx_nested = true;
	(void)cmd_process_line_locked(line, false);
	s_cmd_tx_nested = false;

	if (s_uart_mutex_ready) {
		(void)k_mutex_unlock(&s_uart_mutex);
	}
}

bool cmd_process_line_remote(const char *line, char *out, size_t out_sz, size_t *out_len)
{
	bool ok;

	if (out_len != NULL) {
		*out_len = 0U;
	}

	if ((out != NULL) && (out_sz > 0U)) {
		out[0] = '\0';
	}

	if (s_uart_mutex_ready) {
		(void)k_mutex_lock(&s_uart_mutex, K_FOREVER);
	}

	s_cap_buf = out;
	s_cap_sz = out_sz;
	s_cap_len = 0U;
	s_cap_active = (out != NULL) && (out_sz > 0U);
	s_cmd_tx_nested = true;

	ok = cmd_process_line_locked(line, true);

	if (s_cap_active && (out != NULL) && (out_sz > 0U)) {
		out[(s_cap_len < (out_sz - 1U)) ? s_cap_len : (out_sz - 1U)] = '\0';
	}

	if (out_len != NULL) {
		*out_len = s_cap_len;
	}

	s_cmd_tx_nested = false;
	s_cap_active = false;
	s_cap_buf = NULL;
	s_cap_sz = 0U;
	s_cap_len = 0U;

	if (s_uart_mutex_ready) {
		(void)k_mutex_unlock(&s_uart_mutex);
	}

	return ok;
}

void cmd_feed_char(char ch)
{
	if ((ch == '\b') || (ch == 0x7FU)) {
		if (s_line_len > 0U) {
			s_line_len--;
		}
		return;
	}

	if ((ch == '\r') || (ch == '\n')) {
		if (s_line_len > 0U) {
			s_line_buf[s_line_len] = '\0';
			cmd_process_line(s_line_buf);
			s_line_len = 0U;
		}
		return;
	}

	if (s_line_len < (sizeof(s_line_buf) - 1U)) {
		s_line_buf[s_line_len++] = ch;
	}
}

void cmd_poll(void)
{
	/* RX is handled by cmd_rx_thread (console_getchar). Kept for API compat. */
}

static void cmd_rx_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	(void)console_init();

	while (1) {
		int ch = console_getchar();

		if (ch < 0) {
			continue;
		}
		cmd_feed_char((char)ch);
	}
}

static void cmd_rx_start(void)
{
	if (s_cmd_rx_started) {
		return;
	}

	(void)k_thread_create(&s_cmd_rx_thread, s_cmd_rx_stack,
			      K_THREAD_STACK_SIZEOF(s_cmd_rx_stack), cmd_rx_thread_fn, NULL, NULL,
			      NULL, CMD_RX_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&s_cmd_rx_thread, "cmd_rx");
	s_cmd_rx_started = true;
}

static void cmd_reboot(int argc, const char *argv[])
{
	(void)argc;
	(void)argv;
	cmd_reply_ok("reboot", "now");
	k_msleep(50);
	sys_reboot(SYS_REBOOT_COLD);
}

static void cmd_log_test(int argc, const char *argv[])
{
	(void)argc;
	(void)argv;
	LOG_INFO("[CMD] uart cmd log test ok");
	cmd_reply_ok("log", "ok");
}

static void cmd_version(int argc, const char *argv[])
{
	(void)argc;
	(void)argv;
	cmd_reply_ok("version", version_get_string());
}

static void cmd_sn(int argc, const char *argv[])
{
	char sn[NVS_SN_SIZE];

	if (argc == 1) {
		if (!nvs_sn_get(sn)) {
			cmd_reply_ng();
			return;
		}
		cmd_reply_ok("sn", sn);
		return;
	}

	if ((argc == 2) && (argv[1] != NULL) && (argv[1][0] != '\0')) {
		if (strlen(argv[1]) >= NVS_SN_SIZE) {
			cmd_reply_ng();
			return;
		}
		if (!nvs_sn_set(argv[1])) {
			cmd_reply_ng();
			return;
		}
		cmd_reply_ok("sn", argv[1]);
		return;
	}

	cmd_reply_ng();
}

static void cmd_ftmexit(int argc, const char *argv[])
{
	(void)argc;
	(void)argv;

	/* Clear factory bypass so MCUBoot chainloads image-0 (slot0 / PART_APP_A). */
	if (!nvs_boot_mode_set(NVS_BOOT_MODE_APP)) {
		cmd_reply_ng();
		return;
	}

	cmd_reply_ok("ftmexit", "reboot");
	k_msleep(50);
	sys_reboot(SYS_REBOOT_COLD);
}

static int hex_nibble(char c)
{
	if ((c >= '0') && (c <= '9')) {
		return c - '0';
	}
	if ((c >= 'a') && (c <= 'f')) {
		return c - 'a' + 10;
	}
	if ((c >= 'A') && (c <= 'F')) {
		return c - 'A' + 10;
	}
	return -1;
}

/** Parse contiguous hex (e.g. 987A6BC54DEF0123) into @p out; pads MSBs with 0. */
static bool mac_parse_hex(const char *s, uint8_t *out, size_t out_len)
{
	size_t n;
	size_t i;
	size_t pad;

	if ((s == NULL) || (out == NULL) || (out_len == 0U)) {
		return false;
	}

	n = strlen(s);
	if ((n == 0U) || ((n % 2U) != 0U) || (n > (out_len * 2U))) {
		return false;
	}

	(void)memset(out, 0, out_len);
	pad = out_len - (n / 2U);

	for (i = 0U; i < (n / 2U); i++) {
		int hi = hex_nibble(s[i * 2U]);
		int lo = hex_nibble(s[i * 2U + 1U]);

		if ((hi < 0) || (lo < 0)) {
			return false;
		}
		out[pad + i] = (uint8_t)((hi << 4) | lo);
	}

	return true;
}

static void mac_format_hex(const uint8_t *mac, size_t mac_len, char *buf, size_t buf_len)
{
	size_t i;
	int n = 0;

	if ((mac == NULL) || (buf == NULL) || (buf_len < ((mac_len * 2U) + 1U))) {
		if ((buf != NULL) && (buf_len > 0U)) {
			buf[0] = '\0';
		}
		return;
	}

	for (i = 0U; i < mac_len; i++) {
		n += snprintk(&buf[n], buf_len - (size_t)n, "%02X", mac[i]);
		if ((n < 0) || ((size_t)n >= buf_len)) {
			buf[0] = '\0';
			return;
		}
	}
}

static void cmd_mac(int argc, const char *argv[])
{
	uint8_t mac[NVS_MAC_SIZE];
	char buf[(NVS_MAC_SIZE * 2U) + 1U];

	if (argc == 1) {
		if (!nvs_mac_get(mac)) {
			cmd_reply_ng();
			return;
		}
		mac_format_hex(mac, NVS_MAC_SIZE, buf, sizeof(buf));
		cmd_reply_ok("mac", buf);
		return;
	}

	if ((argc == 2) && (argv[1] != NULL)) {
		if (!mac_parse_hex(argv[1], mac, NVS_MAC_SIZE)) {
			cmd_reply_ng();
			return;
		}
		if (!nvs_mac_set(mac)) {
			cmd_reply_ng();
			return;
		}
		mac_format_hex(mac, NVS_MAC_SIZE, buf, sizeof(buf));
		cmd_reply_ok("mac", buf);
		return;
	}

	cmd_reply_ng();
}

static void cmd_led(int argc, const char *argv[])
{
	bool all = false;
	bool on;
	led_scene_led_e led = LED_SCENE_LED_MAX_NUM;
	const char *which;
	const char *state;

	/* led on|off  → all */
	if (argc == 2) {
		which = "all";
		state = argv[1];
	} else if (argc == 3) {
		which = argv[1];
		state = argv[2];
	} else {
		cmd_reply_ng();
		return;
	}

	if (strcmp(state, "on") == 0) {
		on = true;
	} else if (strcmp(state, "off") == 0) {
		on = false;
	} else {
		cmd_reply_ng();
		return;
	}

	if ((strcmp(which, "all") == 0) || (strcmp(which, "a") == 0)) {
		all = true;
	} else if ((strcmp(which, "r") == 0) || (strcmp(which, "g") == 0) ||
		   (strcmp(which, "0") == 0) || (strcmp(which, "led0") == 0)) {
		led = LED_SCENE_LED_0;
	} else if ((strcmp(which, "b") == 0) || (strcmp(which, "1") == 0) ||
		   (strcmp(which, "led1") == 0)) {
		led = LED_SCENE_LED_1;
	} else {
		cmd_reply_ng();
		return;
	}

	if (all) {
		led_scene_led_direct_set(LED_SCENE_LED_0, on);
		led_scene_led_direct_set(LED_SCENE_LED_1, on);
	} else {
		led_scene_led_direct_set(led, on);
	}

	cmd_reply_ok("led", on ? "on" : "off");
}

static void cmd_btn(int argc, const char *argv[])
{
	btn_id_e id = BTN_ID_CONFIRM;
	uint8_t level;
	char buf[4];

	/* factory board: single FTM key as CONFIRM */
	if (argc == 2) {
		if ((strcmp(argv[1], "ftm") == 0) || (strcmp(argv[1], "confirm") == 0) ||
		    (strcmp(argv[1], "0") == 0)) {
			id = BTN_ID_CONFIRM;
		} else {
			cmd_reply_ng();
			return;
		}
	} else if (argc != 1) {
		cmd_reply_ng();
		return;
	}

	level = button_get_level(id);
	if (level > 1U) {
		cmd_reply_ng();
		return;
	}

	(void)snprintk(buf, sizeof(buf), "%u", (unsigned)level);
	cmd_reply_ok("btn", buf);
}

#ifndef UICR_APPROTECT_PROTECT0_PALL_Protected
#define UICR_APPROTECT_PROTECT0_PALL_Protected (0x00000000UL)
#endif

static bool ccfg_uicr_word_write(volatile uint32_t *reg, uint32_t value)
{
	if (reg == NULL) {
		return false;
	}

	nrfx_rramc_write_enable_set(true, 0);
	nrfx_rramc_word_write((uint32_t)(uintptr_t)reg, value);
	while (!nrfx_rramc_ready_check()) {
	}
	nrfx_rramc_write_buffer_commit();
	while (!nrfx_rramc_ready_check()) {
	}
	nrfx_rramc_write_enable_set(false, 0);

	/* UICR: bits only 1→0; locked if no longer fully Unprotected. */
	return true;
}

static bool ccfg_is_approtect_locked(void)
{
	uint32_t v = NRF_UICR->APPROTECT[0].PROTECT0;

	return v != UICR_APPROTECT_PROTECT0_PALL_Unprotected;
}

static void cmd_ccfg(int argc, const char *argv[])
{
	/*
	 * App debug/readback protect only (UICR APPROTECT). No ERASEPROTECT.
	 * Policy (plan_zigbee_factory_temp_coord §ccfg):
	 * - Never auto-lock on boot; separate APP/factory hex flash stays usable
	 * - Factory: user manually `ccfg lock`; if unlocked, do not auto re-lock
	 * - Future OTA success path may auto-lock; unlock still needs recover
	 */
	if (argc == 1) {
		cmd_reply_ok("ccfg", ccfg_is_approtect_locked() ? "lock" : "unlock");
		return;
	}

	if (argc != 2) {
		cmd_reply_ng();
		return;
	}

	if (strcmp(argv[1], "lock") == 0) {
		(void)ccfg_uicr_word_write(&NRF_UICR->APPROTECT[0].PROTECT0,
					   UICR_APPROTECT_PROTECT0_PALL_Protected);
		cmd_reply_ok("ccfg", ccfg_is_approtect_locked() ? "lock" : "unlock");
		return;
	}

	if (strcmp(argv[1], "unlock") == 0) {
		/* UICR 1→0 only; clear needs CTRL-AP recover / erase. */
		cmd_reply_ok("ccfg", "need_recover");
		return;
	}

	cmd_reply_ng();
}

/**
 * R1+M2 Zigbee FTM surface:
 *   zb tool|uut|status|ch  (aliases: coord=tool, dut=uut)
 *   zb allow ieee|sn|clear ; zb join [sec]
 */
static void cmd_zb_enter_role(zbft_mode_t mode, const char *tag)
{
	static char ieee_s[24];
	zbft_enter_result_t r = zbft_role_enter(mode);

	if (r == ZBFT_ENTER_OK) {
		/* UUT：直接回 ieee，便于 TOOL 侧 zb join <ieee> */
		if ((mode == ZBFT_MODE_UUT) && (zbft_ieee_snprintk(ieee_s, sizeof(ieee_s)) >= 0)) {
			cmd_reply_ok("zb", ieee_s);
			return;
		}
		cmd_reply_ok("zb", tag);
		return;
	}
	if (r == ZBFT_ENTER_REBOOTING) {
		cmd_reply_ok("zb", "switch_reboot");
		return;
	}
	cmd_reply_ng();
}

static void cmd_zb_allow(int argc, const char *argv[])
{
	if ((argc < 3) || (argv[2] == NULL)) {
		cmd_reply_ng();
		return;
	}

	if (strcmp(argv[2], "clear") == 0) {
		if (zbft_mode_get() != ZBFT_MODE_TOOL) {
			cmd_reply_ok("zb", "ng");
			return;
		}
		zbft_allow_clear();
		cmd_reply_ok("zb", "allow_clear");
		return;
	}

	if ((strcmp(argv[2], "ieee") == 0) && (argc >= 4) && (argv[3] != NULL)) {
		if (!zbft_allow_ieee_set(argv[3])) {
			cmd_reply_ok("zb", "ng");
			return;
		}
		/* allow 成功会自动开窗（已 formed 时） */
		cmd_reply_ok("zb", "allow_ieee");
		return;
	}

	if ((strcmp(argv[2], "sn") == 0) && (argc >= 4) && (argv[3] != NULL)) {
		if (!zbft_allow_sn_set(argv[3])) {
			cmd_reply_ok("zb", "ng");
			return;
		}
		cmd_reply_ok("zb", "allow_sn");
		return;
	}

	cmd_reply_ng();
}

static void cmd_zb_join(int argc, const char *argv[])
{
	unsigned long sec = 60UL;
	char *end = NULL;

	if (argc >= 3) {
		if (argv[2] == NULL) {
			cmd_reply_ng();
			return;
		}
		/* zb join <ieee> — 登记白名单并开窗（最简路径） */
		if (zbft_allow_ieee_set(argv[2])) {
			cmd_reply_ok("zb", "join");
			return;
		}
		/* else treat as seconds */
		sec = strtoul(argv[2], &end, 10);
		if ((end == argv[2]) || ((end != NULL) && (*end != '\0'))) {
			cmd_reply_ng();
			return;
		}
	}

	if (!zbft_allow_ieee_is_set()) {
		cmd_reply_ok("zb", "ng");
		return;
	}
	if (!zbft_join_open((uint8_t)sec)) {
		cmd_reply_ok("zb", "ng");
		return;
	}
	cmd_reply_ok("zb", "join");
}

static void cmd_zb(int argc, const char *argv[])
{
	static char st[220];

	if ((argc < 2) || (argv[1] == NULL)) {
		cmd_reply_ng();
		return;
	}

	if (strcmp(argv[1], "status") == 0) {
		if (zbft_status_snprintk(st, sizeof(st)) < 0) {
			cmd_reply_ng();
			return;
		}
		cmd_reply_ok("zb", st);
		return;
	}

	if ((strcmp(argv[1], "tool") == 0) || (strcmp(argv[1], "coord") == 0)) {
		cmd_zb_enter_role(ZBFT_MODE_TOOL, "tool");
		return;
	}

	if ((strcmp(argv[1], "uut") == 0) || (strcmp(argv[1], "dut") == 0)) {
		cmd_zb_enter_role(ZBFT_MODE_UUT, "uut");
		return;
	}

	if (strcmp(argv[1], "ch") == 0) {
		if (argc == 2) {
			(void)snprintk(st, sizeof(st), "%u", (unsigned)zbft_channel_get());
			cmd_reply_ok("zb", st);
			return;
		}
		if (argc == 3) {
			char *end = NULL;
			unsigned long ch = strtoul(argv[2], &end, 10);

			if ((end == argv[2]) || ((end != NULL) && (*end != '\0'))) {
				cmd_reply_ng();
				return;
			}
			if (!zbft_channel_set((uint8_t)ch)) {
				cmd_reply_ok("zb", zbft_stack_started() ? "busy" : "ng");
				return;
			}
			(void)snprintk(st, sizeof(st), "%u", (unsigned)zbft_channel_get());
			cmd_reply_ok("zb", st);
			return;
		}
		cmd_reply_ng();
		return;
	}

	if (strcmp(argv[1], "allow") == 0) {
		cmd_zb_allow(argc, argv);
		return;
	}

	if (strcmp(argv[1], "join") == 0) {
		cmd_zb_join(argc, argv);
		return;
	}

	cmd_reply_ng();
}

static void cmd_zbcmd(int argc, const char *argv[])
{
	static char line[CMD_LINE_MAX];
	static char reply[CMD_LINE_MAX];
	size_t off = 0U;
	int i;

	if (argc < 2) {
		cmd_reply_ng();
		return;
	}
	if ((zbft_mode_get() != ZBFT_MODE_TOOL) || !zbft_dut_joined()) {
		cmd_reply_ok("zbcmd", "ng");
		return;
	}

	line[0] = '\0';
	for (i = 1; i < argc; i++) {
		int n;

		if (argv[i] == NULL) {
			cmd_reply_ng();
			return;
		}
		n = snprintk(line + off, sizeof(line) - off, "%s%s", (off > 0U) ? " " : "",
			     argv[i]);
		if (n < 0) {
			cmd_reply_ng();
			return;
		}
		off += (size_t)n;
		if (off >= (sizeof(line) - 1U)) {
			cmd_reply_ok("zbcmd", "ng");
			return;
		}
	}

	if (!zbft_zbcmd_proxy(line, reply, sizeof(reply), 3000U)) {
		cmd_reply_ok("zbcmd", "ng");
		return;
	}

	/* Optional allow.sn check on bare `sn` get. */
	if ((argc == 2) && (strcmp(argv[1], "sn") == 0) && zbft_allow_sn_is_set()) {
		const char *val = reply;
		const char *colon = strchr(reply, ':');

		if (colon != NULL) {
			val = colon + 1;
		}
		if (strcmp(val, zbft_allow_sn_get()) != 0) {
			printk("zbft: sn warn expect=%s got=%s\n", zbft_allow_sn_get(), val);
		}
	}

	cmd_reply_ok("zbcmd", reply);
}

void cmd_set_project_register_fn(void (*fn)(void))
{
	s_project_register_fn = fn;
}

static void cmd_invoke_project_register_once(void)
{
	if (s_project_register_done || (s_project_register_fn == NULL)) {
		return;
	}

	s_project_register_fn();
	s_project_register_done = true;
}

void cmd_register_defaults(void)
{
	/* Controllable set: air-eligible by default (plan Q3). */
	(void)cmd_register("reboot", cmd_reboot, "software reset");
	(void)cmd_register("log", cmd_log_test, "emit one log line + ok");
	(void)cmd_register("version", cmd_version, "app version string");
	(void)cmd_register("sn", cmd_sn, "get/set SN: sn | sn <value>");
	(void)cmd_register("mac", cmd_mac, "mac | mac <HEX> (no colon, e.g. 987A6BC5..)");
	(void)cmd_register("led", cmd_led, "led on|off | led r|b|all on|off");
	(void)cmd_register("btn", cmd_btn, "btn | btn ftm  → level 0/1");
	(void)cmd_register_local("ccfg", cmd_ccfg,
				 "ccfg|lock|unlock; manual; no auto; OTA may lock later");
	(void)cmd_register("ftmexit", cmd_ftmexit,
			  "exit factory: boot_mode=app + reboot -> image-0");

	/* Zigbee FTM: UART-only control plane (not air-proxyable). */
	(void)cmd_register_local("zb", cmd_zb,
				 "zb tool|uut|status|ch|allow|join <ieee>|join [sec]");
	(void)cmd_register_local("zbcmd", cmd_zbcmd, "zbcmd <factory-line> → DUT");

	cmd_invoke_project_register_once();
	cmd_rx_start();
}
