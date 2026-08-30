/**
 * @file log.c
 * @brief Logging implementation for Zephyr/NCS (API from ESP32/STM32 common).
 */

#include "log.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define LOG_BUFFER_SIZE 256

static struct k_mutex s_log_mutex;
static bool s_log_mutex_ready;
static log_output_func_t s_log_output_func;

static struct
{
    log_level_t level;
    log_color_t color;
    bool timestamp_enable;
    bool file_line_enable;
    bool initialized;
} s_log_config = {
    .level = LOG_LEVEL_INFO,
    .color = LOG_COLOR_NONE,
    .timestamp_enable = true,
    .file_line_enable = false,
    .initialized = false,
};

static const char *s_log_level_strings[] = {
    [LOG_LEVEL_NONE] = "NONE",
    [LOG_LEVEL_FATAL] = "FATAL",
    [LOG_LEVEL_ERROR] = "ERROR",
    [LOG_LEVEL_WARN] = "WARN",
    [LOG_LEVEL_INFO] = "INFO",
    [LOG_LEVEL_DEBUG] = "DEBUG",
    [LOG_LEVEL_VERBOSE] = "VERBOSE",
};

static void log_lock(void)
{
    if (s_log_mutex_ready)
    {
        (void)k_mutex_lock(&s_log_mutex, K_FOREVER);
    }
}

static void log_unlock(void)
{
    if (s_log_mutex_ready)
    {
        (void)k_mutex_unlock(&s_log_mutex);
    }
}

static void log_output_string(const char *str, uint16_t len)
{
    if ((str == NULL) || (len == 0U))
    {
        return;
    }

    if (s_log_output_func != NULL)
    {
        s_log_output_func(str, len);
        return;
    }

    /* Default: Zephyr console / printk path (UART when CONFIG_UART_CONSOLE=y). */
    for (uint16_t i = 0U; i < len; i++)
    {
        printk("%c", str[i]);
    }
}

static const char *log_get_filename(const char *filepath)
{
    const char *filename;

    if (filepath == NULL)
    {
        return "";
    }

    filename = strrchr(filepath, '/');
    if (filename != NULL)
    {
        return filename + 1;
    }

    filename = strrchr(filepath, '\\');
    if (filename != NULL)
    {
        return filename + 1;
    }

    return filepath;
}

static uint32_t log_get_timestamp_ms(void)
{
    return k_uptime_get_32();
}

log_level_t log_get_level(void)
{
    return s_log_config.level;
}

status_t log_set_level(log_level_t level)
{
    if (level >= LOG_LEVEL_MAX)
    {
        return STATUS_INVALID_ARG;
    }

    s_log_config.level = level;
    return STATUS_OK;
}

status_t log_set_color(log_color_t color)
{
    s_log_config.color = color;
    return STATUS_OK;
}

status_t log_set_timestamp(bool enable)
{
    s_log_config.timestamp_enable = enable;
    return STATUS_OK;
}

status_t log_set_file_line(bool enable)
{
    s_log_config.file_line_enable = enable;
    return STATUS_OK;
}

status_t log_init(log_output_func_t output_func)
{
    if (!s_log_mutex_ready)
    {
        (void)k_mutex_init(&s_log_mutex);
        s_log_mutex_ready = true;
    }

    s_log_output_func = output_func;
    s_log_config.initialized = true;
    return STATUS_OK;
}

status_t log_deinit(void)
{
    s_log_config.initialized = false;
    s_log_output_func = NULL;
    return STATUS_OK;
}

void log_output(log_level_t level, const char *file, uint16_t line, const char *fmt, ...)
{
#ifdef CONFIG_APP_BUILD_RELEASE
    /* Release: all common/log output is a no-op (empty). */
    (void)level;
    (void)file;
    (void)line;
    (void)fmt;
    return;
#else
    char buffer[LOG_BUFFER_SIZE];
    int pos = 0;
    va_list args;

    if ((!s_log_config.initialized) || (level == LOG_LEVEL_NONE) || (level >= LOG_LEVEL_MAX))
    {
        return;
    }

    if (level > s_log_config.level)
    {
        return;
    }

    log_lock();

    if (s_log_config.timestamp_enable)
    {
        uint32_t timestamp = log_get_timestamp_ms();
        int ts_len = snprintk(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), "[%05u] ",
                              (unsigned int)timestamp);
        if ((ts_len > 0) && ((pos + ts_len) < LOG_BUFFER_SIZE))
        {
            pos += ts_len;
        }
    }

    {
        int level_len = snprintk(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), "[%s] ",
                                 s_log_level_strings[level]);
        if ((level_len > 0) && ((pos + level_len) < LOG_BUFFER_SIZE))
        {
            pos += level_len;
        }
    }

    if (s_log_config.file_line_enable && (file != NULL))
    {
        const char *filename = log_get_filename(file);
        int fl_len = snprintk(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), "%s:%u ", filename,
                              (unsigned int)line);
        if ((fl_len > 0) && ((pos + fl_len) < LOG_BUFFER_SIZE))
        {
            pos += fl_len;
        }
    }

    va_start(args, fmt);
    {
        int fmt_len = vsnprintk(buffer + pos, (size_t)(LOG_BUFFER_SIZE - pos), fmt, args);
        if ((fmt_len > 0) && ((pos + fmt_len) < LOG_BUFFER_SIZE))
        {
            pos += fmt_len;
        }
    }
    va_end(args);

    if (pos < (LOG_BUFFER_SIZE - 2))
    {
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }
    else
    {
        pos = LOG_BUFFER_SIZE - 2;
        buffer[pos++] = '\r';
        buffer[pos++] = '\n';
    }

    (void)s_log_config.color;
    log_output_string(buffer, (uint16_t)pos);
    log_unlock();
#endif /* CONFIG_APP_BUILD_RELEASE */
}
