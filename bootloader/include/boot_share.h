/**
 * @file boot_share.h
 * @brief Boot ↔ APP bypass page at end of storage (doc/flash_layout.md).
 *
 * Boot reads this page only (no Zephyr Settings parse).
 * APP nvs_boot_mode_set() dual-writes Settings + this record.
 */

#ifndef BOOTLOADER_BOOT_SHARE_H
#define BOOTLOADER_BOOT_SHARE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Absolute RRAM offset within cpuapp (matches flash_layout / pm_static). */
#define BOOT_SHARE_ADDR (0x00164000u)
#define BOOT_SHARE_SIZE (0x00001000u) /* 4 KiB page */

#define BOOT_SHARE_MAGIC   (0x54594253u) /* 'TYBS' */
#define BOOT_SHARE_VERSION (1u)

/** Same values as NVS_BOOT_MODE_* in common/nvs.h */
#define BOOT_SHARE_MODE_APP     (0u)
#define BOOT_SHARE_MODE_FACTORY (1u)

/**
 * Fixed record at start of boot_share page (16-byte aligned).
 * Remaining page reserved for future fields (e.g. boot-loop counter).
 */
struct boot_share_record {
	uint32_t magic;
	uint8_t version;
	uint8_t boot_mode;
	uint8_t reserved[6];
	uint32_t crc32; /* CRC-32 of magic..reserved (common/crc32) */
};

_Static_assert(sizeof(struct boot_share_record) == 16u, "boot_share_record must be 16 bytes");

#ifdef __cplusplus
}
#endif

#endif /* BOOTLOADER_BOOT_SHARE_H */
