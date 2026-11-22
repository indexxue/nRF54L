/**
 * @file page_slot.h
 * @brief W25Q 页槽：home/notes/info 位图存放；RAM 只作暂存与刷屏缓冲
 *
 * 布局见 PRODUCT.md。禁止在 ISR/GATT 回调中调用擦写/读槽。
 */

#ifndef INK_PANEL_PAGE_SLOT_H
#define INK_PANEL_PAGE_SLOT_H

#include "type.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PAGE_SLOT_MODE_HOME  0U
#define PAGE_SLOT_MODE_NOTES 1U
#define PAGE_SLOT_MODE_INFO  2U

#define PAGE_SLOT_HOME_COUNT  1U
#define PAGE_SLOT_NOTES_COUNT 8U
#define PAGE_SLOT_INFO_COUNT  4U

/** 每槽 32 KB（4.2" BWR 30000 + 对齐） */
#define PAGE_SLOT_BYTES 0x8000U

status_t page_slot_init(void);
bool page_slot_is_ready(void);

bool page_slot_mode_page_ok(uint8_t mode, uint8_t page);
bool page_slot_is_valid(uint8_t mode, uint8_t page);

/**
 * 将 BW/RED 平面写入槽并置 valid。
 * @param plane_size 须为当前面板 plane 字节数（4.2"=15000）
 */
status_t page_slot_store(uint8_t mode, uint8_t page, const uint8_t *bw, const uint8_t *red,
			 uint32_t plane_size);

/**
 * 读槽到 BW/RED。invalid 或未写过：BW=0xFF、RED=0x00（白屏）。
 */
status_t page_slot_load(uint8_t mode, uint8_t page, uint8_t *bw, uint8_t *red,
			uint32_t plane_size);

uint32_t page_slot_font_base(void);
uint32_t page_slot_font_size(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_PAGE_SLOT_H */
