/**
 * @file crc32.c
 * @brief CRC-32 implementation (buffer path; from STM32G431 Common).
 */

#include "crc32.h"

#include <stddef.h>

static uint32_t s_crc32_table[256];
static int s_crc32_ready;

void crc32_init_table(void)
{
    uint32_t i;
    uint32_t j;

    if (s_crc32_ready)
    {
        return;
    }

    for (i = 0U; i < 256U; i++)
    {
        uint32_t c = i;

        for (j = 0U; j < 8U; j++)
        {
            if (c & 1U)
            {
                c = 0xEDB88320U ^ (c >> 1);
            }
            else
            {
                c >>= 1;
            }
        }
        s_crc32_table[i] = c;
    }

    s_crc32_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    if (!s_crc32_ready)
    {
        crc32_init_table();
    }

    if (data == NULL)
    {
        return crc;
    }

    for (i = 0U; i < len; i++)
    {
        uint8_t idx = (uint8_t)((crc ^ data[i]) & 0xFFU);

        crc = s_crc32_table[idx] ^ (crc >> 8);
    }

    return crc;
}

uint32_t crc32_finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFU;
}

uint32_t crc32_compute(const uint8_t *data, uint32_t len)
{
    return crc32_finalize(crc32_update(CRC32_INIT_VALUE, data, len));
}
