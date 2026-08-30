/**
 * @file crc32.h
 * @brief CRC-32 (IEEE / Ethernet polynomial) — from STM32G431 Common.
 */

#ifndef CRC32_H
#define CRC32_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define CRC32_INIT_VALUE 0xFFFFFFFFU

void crc32_init_table(void);
uint32_t crc32_compute(const uint8_t *data, uint32_t len);
uint32_t crc32_update(uint32_t crc, const uint8_t *data, uint32_t len);
uint32_t crc32_finalize(uint32_t crc);

#ifdef __cplusplus
}
#endif

#endif /* CRC32_H */
