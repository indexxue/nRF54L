/**
 * @file zbft.h
 * @brief Factory temp Zigbee backdoor (R1–M3): TOOL/UUT, allow/join, zbcmd proxy.
 *
 * - TOOL = temporary Coordinator (form + allow + join + zbcmd)
 * - UUT  = temporary DUT as Router (non-ED ZBOSS lib; ED deferred)
 * - Boot: wipe zboss NV, stay idle (no RF) unless a pending switch req
 * - M2: allow IEEE (RAM, capacity=1); join window; mismatch → leave + deny
 * - M3: zbcmd over custom ZCL cluster → DUT cmd_process_line_remote
 */

#ifndef FACTORY_ZBFT_H
#define FACTORY_ZBFT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	ZBFT_MODE_IDLE = 0,
	ZBFT_MODE_TOOL = 1,
	ZBFT_MODE_UUT = 2,
} zbft_mode_t;

typedef enum {
	ZBFT_ENTER_OK = 0,
	ZBFT_ENTER_REBOOTING = 1,
	ZBFT_ENTER_FAIL = 2,
} zbft_enter_result_t;

/** Boot: wipe zboss NV, keep Settings; honor pending switch req if any. */
status_t zbft_boot(void);

zbft_mode_t zbft_mode_get(void);
uint8_t zbft_channel_get(void);
bool zbft_stack_started(void);
bool zbft_net_up(void);

/**
 * Enter TOOL or UUT.
 * Idle stack: start in place. Same role: OK. Other role: persist + wipe + reboot.
 */
zbft_enter_result_t zbft_role_enter(zbft_mode_t mode);

/** Set channel 11..26 before stack start; false if busy or OOR. */
bool zbft_channel_set(uint8_t ch);

/** Fill status line (no CR/LF); returns bytes written or -1. */
int zbft_status_snprintk(char *buf, size_t buf_len);

/** Local IEEE hex string (no CR/LF); "-" if stack not up. Returns bytes or -1. */
int zbft_ieee_snprintk(char *buf, size_t buf_len);

/**
 * M2 allow / join (TOOL only; RAM session, cleared on reboot).
 * ieee hex: 16 nibbles, optional 0x, big-endian text (same as parse_long_address).
 */
bool zbft_allow_ieee_set(const char *hex16);
bool zbft_allow_sn_set(const char *sn);
void zbft_allow_clear(void);
bool zbft_allow_ieee_is_set(void);
bool zbft_allow_sn_is_set(void);
/** Pointer valid while allow.sn set; else "". */
const char *zbft_allow_sn_get(void);

/** Open permit-join window (1..254 s). Needs TOOL + net up + allow ieee. */
bool zbft_join_open(uint8_t sec);

bool zbft_dut_joined(void);

/**
 * M3: TOOL proxy factory line to joined DUT; blocks until reply or timeout.
 * @param out reply text (trimmed, no CR/LF); may be NULL
 * @return true if DUT replied; false → caller prints ng (not joined / busy / timeout)
 */
bool zbft_zbcmd_proxy(const char *line, char *out, size_t out_sz, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_ZBFT_H */
