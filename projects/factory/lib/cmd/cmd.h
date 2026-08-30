/**
 * @file cmd.h
 * @brief Factory serial line command framework (project-private; not in common/).
 *
 * Reply format: success `cmd:value\r\n`, failure `ng\r\n`. Built-in `help`.
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
#define CMD_LINE_MAX            (200U)
#define CMD_NAME_MAX            (16U)
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

#ifdef __cplusplus
}
#endif

#endif /* FACTORY_CMD_H */
