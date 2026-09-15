/**
 * @file cmd.c
 * @brief Factory command framework (Zephyr). Lives under projects/factory/lib/cmd/.
 */

#include "cmd.h"

#include "log.h"
#include "fw_version.h"
#include "nvs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>

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

static bool cmd_write_printk(const void *data, size_t len, void *user_ctx)
{
    const char *p = (const char *)data;

    (void)user_ctx;
    if ((data == NULL) || (len == 0U))
    {
        return true;
    }

    for (size_t i = 0U; i < len; i++)
    {
        printk("%c", p[i]);
    }

    return true;
}

void cmd_init(cmd_write_fn write_fn, void *write_ctx)
{
    if (!s_uart_mutex_ready)
    {
        (void)k_mutex_init(&s_uart_mutex);
        s_uart_mutex_ready = true;
    }

    s_write_fn = (write_fn != NULL) ? write_fn : cmd_write_printk;
    s_write_ctx = write_ctx;
    s_cmd_count = 0;
    (void)memset(s_cmd_table, 0, sizeof(s_cmd_table));
    s_line_len = 0U;
}

int cmd_register(const char *name, cmd_handler_t handler, const char *help)
{
    size_t nlen;

    if ((name == NULL) || (handler == NULL) || (s_cmd_count >= (int)CMD_MAX_COMMANDS))
    {
        return -1;
    }

    nlen = strlen(name);
    if ((nlen == 0U) || (nlen >= CMD_NAME_MAX))
    {
        return -1;
    }

    (void)strncpy(s_cmd_table[s_cmd_count].name, name, CMD_NAME_MAX - 1U);
    s_cmd_table[s_cmd_count].name[CMD_NAME_MAX - 1U] = '\0';
    s_cmd_table[s_cmd_count].handler = handler;

    if (help != NULL)
    {
        (void)strncpy(s_cmd_table[s_cmd_count].help, help, CMD_HELP_MAX - 1U);
        s_cmd_table[s_cmd_count].help[CMD_HELP_MAX - 1U] = '\0';
    }
    else
    {
        s_cmd_table[s_cmd_count].help[0] = '\0';
    }

    s_cmd_count++;
    return 0;
}

static bool cmd_send_unlocked(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U) || (s_write_fn == NULL))
    {
        return false;
    }

    return s_write_fn(data, (size_t)len, s_write_ctx);
}

bool cmd_send(const uint8_t *data, uint16_t len)
{
    bool ok;

    if ((data == NULL) || (len == 0U))
    {
        return false;
    }

    if (s_uart_mutex_ready)
    {
        if (k_mutex_lock(&s_uart_mutex, K_MSEC(CMD_TX_MUTEX_TIMEOUT_MS)) != 0)
        {
            return false;
        }
    }

    ok = cmd_send_unlocked(data, len);

    if (s_uart_mutex_ready)
    {
        (void)k_mutex_unlock(&s_uart_mutex);
    }

    return ok;
}

void cmd_uart_lock(void)
{
    if (s_uart_mutex_ready)
    {
        (void)k_mutex_lock(&s_uart_mutex, K_FOREVER);
    }
}

void cmd_uart_unlock(void)
{
    if (s_uart_mutex_ready)
    {
        (void)k_mutex_unlock(&s_uart_mutex);
    }
}

bool cmd_send_str(const char *str)
{
    size_t len;

    if (str == NULL)
    {
        return false;
    }

    len = strlen(str);
    if (len == 0U)
    {
        return true;
    }

    if (len > 0xFFFFU)
    {
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
    if ((n > 0) && ((size_t)n < sizeof(buf)))
    {
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
    while ((*line == ' ') || (*line == '\t'))
    {
        line++;
    }

    while ((*line != '\0') && (*argc < max_argc))
    {
        argv[*argc] = line;
        (*argc)++;
        while ((*line != '\0') && (*line != ' ') && (*line != '\t') && (*line != '\r') &&
               (*line != '\n'))
        {
            *line = (char)tolower((unsigned char)*line);
            line++;
        }

        if (*line == '\0')
        {
            break;
        }

        *line = '\0';
        line++;
        while ((*line == ' ') || (*line == '\t'))
        {
            line++;
        }
    }
}

static void do_help(void)
{
    char buf[CMD_STATUS_BUF_SIZE];
    int n;

    n = snprintk(buf, sizeof(buf), "help: list commands and usage\r\n");
    if ((n > 0) && ((size_t)n < sizeof(buf)))
    {
        (void)cmd_send((const uint8_t *)buf, (uint16_t)n);
    }

    for (int i = 0; i < s_cmd_count; i++)
    {
        const char *h = s_cmd_table[i].help;

        if (h[0] == '\0')
        {
            h = "";
        }

        n = snprintk(buf, sizeof(buf), "%s: %s\r\n", s_cmd_table[i].name, h);
        if ((n > 0) && ((size_t)n < sizeof(buf)))
        {
            (void)cmd_send((const uint8_t *)buf, (uint16_t)n);
        }
    }
}

static void cmd_process_line_locked(const char *line)
{
    char buf[CMD_LINE_MAX];
    size_t len;
    const char *argv[CMD_MAX_ARGC];
    int argc = 0;

    if (line == NULL)
    {
        cmd_reply_ng();
        return;
    }

    len = strlen(line);
    if (len >= sizeof(buf))
    {
        len = sizeof(buf) - 1U;
    }

    (void)memcpy(buf, line, len);
    buf[len] = '\0';

    for (size_t i = 0U; i < len; i++)
    {
        if ((buf[i] == '\r') || (buf[i] == '\n'))
        {
            buf[i] = '\0';
            break;
        }
    }

    {
        char *space_pos = strchr(buf, ' ');
        char *tab_pos = strchr(buf, '\t');
        char *sep = strchr(buf, ':');
        char *first_blank = space_pos;

        if ((first_blank == NULL) || ((tab_pos != NULL) && (tab_pos < first_blank)))
        {
            first_blank = tab_pos;
        }

        if ((sep != NULL) && ((first_blank == NULL) || (sep < first_blank)))
        {
            *sep = ' ';
        }
    }

    trim_and_tokenize(buf, argv, &argc, (int)CMD_MAX_ARGC);
    if (argc == 0)
    {
        cmd_reply_ng();
        return;
    }

    if (strcmp(argv[0], "help") == 0)
    {
        do_help();
        return;
    }

    for (int i = 0; i < s_cmd_count; i++)
    {
        if (strcmp(argv[0], s_cmd_table[i].name) == 0)
        {
            s_cmd_table[i].handler(argc, argv);
            return;
        }
    }

    cmd_reply_ng();
}

void cmd_process_line(const char *line)
{
    if (s_uart_mutex_ready)
    {
        (void)k_mutex_lock(&s_uart_mutex, K_FOREVER);
    }

    cmd_process_line_locked(line);

    if (s_uart_mutex_ready)
    {
        (void)k_mutex_unlock(&s_uart_mutex);
    }
}

void cmd_feed_char(char ch)
{
    if ((ch == '\b') || (ch == 0x7FU))
    {
        if (s_line_len > 0U)
        {
            s_line_len--;
        }
        return;
    }

    if ((ch == '\r') || (ch == '\n'))
    {
        if (s_line_len > 0U)
        {
            s_line_buf[s_line_len] = '\0';
            cmd_process_line(s_line_buf);
            s_line_len = 0U;
        }
        return;
    }

    if (s_line_len < (sizeof(s_line_buf) - 1U))
    {
        s_line_buf[s_line_len++] = ch;
    }
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

    if (argc == 1)
    {
        if (!nvs_sn_get(sn))
        {
            cmd_reply_ng();
            return;
        }
        cmd_reply_ok("sn", sn);
        return;
    }

    if ((argc == 2) && (argv[1] != NULL) && (argv[1][0] != '\0'))
    {
        if (strlen(argv[1]) >= NVS_SN_SIZE)
        {
            cmd_reply_ng();
            return;
        }
        if (!nvs_sn_set(argv[1]))
        {
            cmd_reply_ng();
            return;
        }
        cmd_reply_ok("sn", argv[1]);
        return;
    }

    cmd_reply_ng();
}

void cmd_set_project_register_fn(void (*fn)(void))
{
    s_project_register_fn = fn;
}

static void cmd_invoke_project_register_once(void)
{
    if (s_project_register_done || (s_project_register_fn == NULL))
    {
        return;
    }

    s_project_register_fn();
    s_project_register_done = true;
}

void cmd_register_defaults(void)
{
    (void)cmd_register("reboot", cmd_reboot, "software reset");
    (void)cmd_register("log", cmd_log_test, "emit one log line + ok");
    (void)cmd_register("version", cmd_version, "app version string");
    (void)cmd_register("sn", cmd_sn, "get/set SN: sn | sn <value>");
    cmd_invoke_project_register_once();
}
