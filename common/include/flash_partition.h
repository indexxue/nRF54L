/**
 * @file flash_partition.h
 * @brief nRF54L15 应用核 RRAM 分区常量（与 doc/flash_layout.md 一致）
 *
 * 地址为 cpuapp RRAM 内偏移。尾部 96 KB FLPR 不纳入本表可写区。
 * MCUBoot 启用前本头文件供评审与后续 pm_static / 应用层对齐；日常持久化走 Settings → storage。
 */

#ifndef COMMON_FLASH_PARTITION_H
#define COMMON_FLASH_PARTITION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** 应用核可用 RRAM（不含 FLPR 96 KB） */
#define FLASH_CHIP_APP_SIZE (0x00165000u) /* 1428 KiB */

#define FLASH_BOOTLOADER_START (0x00000000u)
#define FLASH_BOOTLOADER_SIZE  (0x00010000u) /* 64 KiB mcuboot */
#define FLASH_BOOTLOADER_END   (FLASH_BOOTLOADER_START + FLASH_BOOTLOADER_SIZE)

#define PART_APP_A_START (0x00010000u) /* image-0 / slot0 */
#define PART_APP_A_SIZE  (0x000A6000u) /* 664 KiB */
#define PART_APP_A_END   (PART_APP_A_START + PART_APP_A_SIZE)

#define PART_APP_B_START (0x000B6000u) /* image-1 / slot1：OTA ∪ 厂测 */
#define PART_APP_B_SIZE  (0x000A6000u) /* 664 KiB */
#define PART_APP_B_END   (PART_APP_B_START + PART_APP_B_SIZE)

#define PART_NVS_START (0x0015C000u) /* storage / Settings */
#define PART_NVS_SIZE  (0x00009000u) /* 36 KiB */
#define PART_NVS_END   (PART_NVS_START + PART_NVS_SIZE)

#define PART_FLPR_START (0x00165000u)
#define PART_FLPR_SIZE  (0x00018000u) /* 96 KiB reserved */
#define PART_FLPR_END   (PART_FLPR_START + PART_FLPR_SIZE)

#define FLASH_CHIP_SIZE (PART_FLPR_END) /* 1524 KiB 片内合计 */

/* STM32 / ESP 工程命名别名 */
#define FLASH_PART_NVS_START PART_NVS_START
#define FLASH_PART_NVS_SIZE  PART_NVS_SIZE
#define FLASH_PART_NVS_END   PART_NVS_END

#define FLASH_PART_APP_A_START PART_APP_A_START
#define FLASH_PART_APP_A_SIZE  PART_APP_A_SIZE
#define FLASH_PART_APP_B_START PART_APP_B_START
#define FLASH_PART_APP_B_SIZE  PART_APP_B_SIZE

_Static_assert(PART_APP_A_END == PART_APP_B_START, "slot0/slot1 must be contiguous");
_Static_assert(PART_APP_B_END == PART_NVS_START, "slot1/storage must be contiguous");
_Static_assert(PART_NVS_END == PART_FLPR_START, "storage/FLPR must be contiguous");
_Static_assert((FLASH_BOOTLOADER_SIZE + PART_APP_A_SIZE + PART_APP_B_SIZE + PART_NVS_SIZE) ==
                   FLASH_CHIP_APP_SIZE,
               "app RRAM map must total 1428 KiB");

#ifdef __cplusplus
}
#endif

#endif /* COMMON_FLASH_PARTITION_H */
