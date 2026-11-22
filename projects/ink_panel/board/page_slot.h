/**
 * @file page_slot.h
 * @brief W25Q 页槽 + meta@0 组信息（标题 / 内容页数）
 *
 * home page0 = 画面；notes/info 的 page0 不用于显示（组 meta 在 Flash meta 区）。
 * 禁止在 ISR/GATT 回调中擦写。
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
#define PAGE_SLOT_NOTES_COUNT 8U /* page0 保留；内容 1..7 */
#define PAGE_SLOT_INFO_COUNT  4U /* page0 保留；内容 1..3 */

#define PAGE_SLOT_NOTES_CONTENT_MAX 7U
#define PAGE_SLOT_INFO_CONTENT_MAX  3U

/** 标题 UTF-8 最长字节（不含 NUL）；缓冲 32 含填充 */
#define PAGE_SLOT_TITLE_MAX 31U
#define PAGE_SLOT_TITLE_BUF 32U

/** 每槽 32 KB（4.2" BWR 30000 + 对齐） */
#define PAGE_SLOT_BYTES 0x8000U

status_t page_slot_init(void);
bool page_slot_is_ready(void);

/** 物理槽是否存在（含 notes/info page0） */
bool page_slot_mode_page_ok(uint8_t mode, uint8_t page);

/** 是否允许作为显示/上传内容页（notes/info 排除 page0） */
bool page_slot_is_content_page(uint8_t mode, uint8_t page);

bool page_slot_is_valid(uint8_t mode, uint8_t page);

/**
 * 将 BW/RED 写入内容槽并置 valid。
 * notes/info 的 page0 → STATUS_INVALID_ARG。
 * 若 page > content_count，自动抬高 content_count。
 */
status_t page_slot_store(uint8_t mode, uint8_t page, const uint8_t *bw, const uint8_t *red,
			 uint32_t plane_size);

/** 读内容槽；invalid → 白屏。page0(notes/info) → INVALID_ARG */
status_t page_slot_load(uint8_t mode, uint8_t page, uint8_t *bw, uint8_t *red,
			uint32_t plane_size);

/** 组内容页数（翻页上界）；home 恒为 1；未配置则为 0 */
uint8_t page_slot_group_content_count(uint8_t mode);

/**
 * 写组 meta（仅 notes/info）。
 * @param title 可为 NULL 且 title_len=0 表示清空标题
 */
status_t page_slot_group_set(uint8_t mode, uint8_t content_count, const uint8_t *title,
			     uint8_t title_len);

/** 读组标题；out_len 返回实际长度；buf 至少 PAGE_SLOT_TITLE_BUF */
status_t page_slot_group_get(uint8_t mode, uint8_t *content_count, uint8_t *title_buf,
			     uint8_t *out_len);

/** notes 内容页 valid 位图：bit0=page1 … bit6=page7 */
uint8_t page_slot_notes_valid_bits(void);

/** info 内容页 valid 位图：bit0=page1 … bit2=page3 */
uint8_t page_slot_info_valid_bits(void);

bool page_slot_home_valid(void);

/** 首页叠层开关（持久化于 meta；与 Settings/NVS 同源 Flash 策略：事件驱动写） */
#define PAGE_SLOT_OVL_PAGES (1U << 0)
#define PAGE_SLOT_OVL_BATT  (1U << 1)

status_t page_slot_home_overlay_set(uint8_t flags);
uint8_t page_slot_home_overlay_get(void);

/** 删除组内最后一页：content_count-- 并清该页 valid */
status_t page_slot_group_delete_last(uint8_t mode);

uint32_t page_slot_font_base(void);
uint32_t page_slot_font_size(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_PAGE_SLOT_H */
