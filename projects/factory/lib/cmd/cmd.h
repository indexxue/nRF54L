/**
 * @file cmd.h
 * @brief Factory serial line command framework (project-private; not in common/).
 *
 * Reply format: success `cmd:value\r\n`, failure `ng\r\n`. Built-in `help`.
 *
 * Length / parse limits (exceed → drop chars, register fail, or `ng`):
 * - CMD_LINE_MAX: whole RX line (until \\r/\\n), including args
 * - CMD_NAME_MAX: registered command name only (e.g. "ftmexit"); argv[0] match is exact
 * - CMD_MAX_ARGC: tokens after split on space/tab
 *
 * Args are separate tokens when needed (space/tab). Unknown argv[0] → `ng`.
 * Factory exit: `ftmexit` → boot_mode=app + cold reboot → Boot takes image-0 (slot0).
 */

#ifndef FACTORY_CMD_H
#define FACTORY_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

#define CMD_TX_MUTEX_TIMEOUT_MS (500U)
#define CMD_LINE_MAX            (200U) /* entire line; longer input is truncated in feed */
#define CMD_NAME_MAX            (16U)  /* register() name length; "ftmexit" is fine */
#define CMD_HELP_MAX            (48U)
#define CMD_MAX_COMMANDS        (32U)
#define CMD_MAX_ARGC            (8U)
#define CMD_STATUS_BUF_SIZE     (128U)

typedef bool (*cmd_write_fn)(const void *data, size_t len, void *user_ctx);
typedef void (*cmd_handler_t)(int argc, const char *argv[]);

typedef struct
{
    char name[CMD_NAME_MAX];
    cmd_handler_t handler;
    char help[CMD_HELP_MAX];
} cmd_entry_t;

void cmd_init(cmd_write_fn write_fn, void *write_ctx);
int cmd_register(const char *name, cmd_handler_t handler, const char *help);
void cmd_reply_ok(const char *cmd, const char *value);
void cmd_reply_ng(void);
bool cmd_send(const uint8_t *data, uint16_t len);
bool cmd_send_str(const char *str);
void cmd_process_line(const char *line);
void cmd_register_defaults(void);
void cmd_set_project_register_fn(void (*fn)(void));
void cmd_uart_lock(void);
void cmd_uart_unlock(void);

/**
 * Feed one RX byte into the line assembler (call from UART poll / IRQ thread).
 * Completes a line on '\\r' or '\\n' and runs cmd_process_line().
 */
void cmd_feed_char(char ch);

/** Drain console UART RX into cmd_feed_char(); no-op when cmd_rx thread is used. */
void cmd_poll(void);

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_CMD_H */
