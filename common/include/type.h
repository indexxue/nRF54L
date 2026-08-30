/**
 * @file type.h
 * @brief Common width aliases and status codes (Zephyr/NCS; API aligned with ESP32/STM32 common).
 */

#ifndef COMMON_TYPE_H
#define COMMON_TYPE_H

#include <stddef.h>
#include <stdint.h>

typedef void void_t;
typedef char char_t;
typedef signed char s8_t;
typedef unsigned char u8_t;
typedef int16_t s16_t;
typedef uint16_t u16_t;
typedef int32_t s32_t;
typedef uint32_t u32_t;
typedef int64_t s64_t;
typedef uint64_t u64_t;
typedef float f32_t;
typedef double f64_t;
typedef size_t usize_t;
typedef void_t *ptr_t;
typedef u8_t bool_t;

#define FALSE ((bool_t)0U)
#define TRUE  ((bool_t)1U)

#ifndef NULL_PTR
#define NULL_PTR ((void_t *)0)
#endif

#define UNUSED(param) ((void_t)(param))

typedef int status_t;

#define STATUS_OK            0
#define STATUS_FAIL          (-1)
#define STATUS_INVALID_ARG   (-2)
#define STATUS_NO_MEM        (-3)
#define STATUS_TIMEOUT       (-4)
#define STATUS_NOT_SUPPORTED (-5)
#define STATUS_INVALID_STATE (-6)

static inline const char *status_to_str(status_t s)
{
    switch (s)
    {
    case STATUS_OK:
        return "OK";
    case STATUS_FAIL:
        return "FAIL";
    case STATUS_INVALID_ARG:
        return "INVALID_ARG";
    case STATUS_NO_MEM:
        return "NO_MEM";
    case STATUS_TIMEOUT:
        return "TIMEOUT";
    case STATUS_NOT_SUPPORTED:
        return "NOT_SUPPORTED";
    case STATUS_INVALID_STATE:
        return "INVALID_STATE";
    default:
        return "UNKNOWN";
    }
}

#endif /* COMMON_TYPE_H */
