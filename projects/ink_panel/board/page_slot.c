/**
 * @file page_slot.c
 * @brief ink_panel W25Q 页槽 + meta v2 组信息
 *
 * meta@0x0000 (4KB) magic IPS2
 * slots@0x1000 : home×1 + notes×8 + info×4，各 32KB
 * font_cjk@0x69000 : 2MB 预留
 */

#include "page_slot.h"

#include "w25q_board.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define PAGE_SLOT_MAGIC   0x32535049U /* 'IPS2' LE */
#define PAGE_SLOT_VER     2U
#define PAGE_SLOT_META_SZ W25Q_SECTOR_SIZE
#define PAGE_SLOT_BASE    PAGE_SLOT_META_SZ
#define PAGE_SLOT_TOTAL \
	(PAGE_SLOT_HOME_COUNT + PAGE_SLOT_NOTES_COUNT + PAGE_SLOT_INFO_COUNT)
#define PAGE_SLOT_FONT_BASE \
	(PAGE_SLOT_BASE + ((uint32_t)PAGE_SLOT_TOTAL * PAGE_SLOT_BYTES))
#define PAGE_SLOT_FONT_SIZE 0x200000U

#define PAGE_SLOT_PAYLOAD_MAX 15000U

typedef struct {
	uint8_t content_count;
	uint8_t reserved;
	uint8_t title_len;
	uint8_t title[PAGE_SLOT_TITLE_BUF];
} page_slot_group_t;

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t slot_bytes;
	uint8_t valid_bits[(PAGE_SLOT_TOTAL + 7U) / 8U];
	uint8_t boot_mode;
	uint8_t boot_page;
	uint8_t reserved0[14];
	page_slot_group_t notes;
	page_slot_group_t info;
} page_slot_meta_t;

static page_slot_meta_t s_meta;
static bool s_ready;
static K_MUTEX_DEFINE(s_meta_lock);

static int page_slot_index(uint8_t mode, uint8_t page)
{
	if (mode == PAGE_SLOT_MODE_HOME) {
		if (page >= PAGE_SLOT_HOME_COUNT) {
			return -1;
		}
		return (int)page;
	}
	if (mode == PAGE_SLOT_MODE_NOTES) {
		if (page >= PAGE_SLOT_NOTES_COUNT) {
			return -1;
		}
		return (int)(PAGE_SLOT_HOME_COUNT + page);
	}
	if (mode == PAGE_SLOT_MODE_INFO) {
		if (page >= PAGE_SLOT_INFO_COUNT) {
			return -1;
		}
		return (int)(PAGE_SLOT_HOME_COUNT + PAGE_SLOT_NOTES_COUNT + page);
	}
	return -1;
}

static uint32_t page_slot_addr(int index)
{
	return PAGE_SLOT_BASE + ((uint32_t)index * PAGE_SLOT_BYTES);
}

static bool page_slot_bit_get(const page_slot_meta_t *m, int index)
{
	uint8_t byte = (uint8_t)((unsigned)index / 8U);
	uint8_t bit = (uint8_t)((unsigned)index % 8U);

	return (m->valid_bits[byte] & (uint8_t)BIT(bit)) != 0U;
}

static void page_slot_bit_set(page_slot_meta_t *m, int index, bool valid)
{
	uint8_t byte = (uint8_t)((unsigned)index / 8U);
	uint8_t bit = (uint8_t)((unsigned)index % 8U);

	if (valid) {
		m->valid_bits[byte] |= (uint8_t)BIT(bit);
	} else {
		m->valid_bits[byte] &= (uint8_t)~BIT(bit);
	}
}

static page_slot_group_t *page_slot_group_ptr(page_slot_meta_t *m, uint8_t mode)
{
	if (mode == PAGE_SLOT_MODE_NOTES) {
		return &m->notes;
	}
	if (mode == PAGE_SLOT_MODE_INFO) {
		return &m->info;
	}
	return NULL;
}

static uint8_t page_slot_content_max(uint8_t mode)
{
	if (mode == PAGE_SLOT_MODE_NOTES) {
		return PAGE_SLOT_NOTES_CONTENT_MAX;
	}
	if (mode == PAGE_SLOT_MODE_INFO) {
		return PAGE_SLOT_INFO_CONTENT_MAX;
	}
	if (mode == PAGE_SLOT_MODE_HOME) {
		return PAGE_SLOT_HOME_COUNT;
	}
	return 0U;
}

static void page_slot_meta_default(page_slot_meta_t *m)
{
	memset(m, 0, sizeof(*m));
	m->magic = PAGE_SLOT_MAGIC;
	m->version = PAGE_SLOT_VER;
	m->slot_bytes = (uint16_t)PAGE_SLOT_BYTES;
	m->boot_mode = PAGE_SLOT_MODE_HOME;
	m->boot_page = 0U;
}

static status_t page_slot_meta_write_locked(const page_slot_meta_t *m)
{
	w25q_status_t st;

	st = w25q_erase_sector(&g_w25q, 0U);
	if (st != W25Q_OK) {
		printk("page_slot: meta erase fail %d\n", (int)st);
		return STATUS_FAIL;
	}

	st = w25q_write(&g_w25q, 0U, (const uint8_t *)m, sizeof(*m));
	if (st != W25Q_OK) {
		printk("page_slot: meta write fail %d\n", (int)st);
		return STATUS_FAIL;
	}
	return STATUS_OK;
}

static status_t page_slot_meta_load(void)
{
	w25q_status_t st;
	page_slot_meta_t tmp;

	st = w25q_read(&g_w25q, 0U, (uint8_t *)&tmp, sizeof(tmp));
	if (st != W25Q_OK) {
		printk("page_slot: meta read fail %d\n", (int)st);
		return STATUS_FAIL;
	}

	if ((tmp.magic != PAGE_SLOT_MAGIC) || (tmp.version != PAGE_SLOT_VER) ||
	    (tmp.slot_bytes != (uint16_t)PAGE_SLOT_BYTES)) {
		printk("page_slot: meta v2 format (was ver=%u)\n", (unsigned)tmp.version);
		page_slot_meta_default(&s_meta);
		return page_slot_meta_write_locked(&s_meta);
	}

	if (tmp.notes.content_count > PAGE_SLOT_NOTES_CONTENT_MAX) {
		tmp.notes.content_count = PAGE_SLOT_NOTES_CONTENT_MAX;
	}
	if (tmp.info.content_count > PAGE_SLOT_INFO_CONTENT_MAX) {
		tmp.info.content_count = PAGE_SLOT_INFO_CONTENT_MAX;
	}
	if (tmp.notes.title_len > PAGE_SLOT_TITLE_MAX) {
		tmp.notes.title_len = PAGE_SLOT_TITLE_MAX;
	}
	if (tmp.info.title_len > PAGE_SLOT_TITLE_MAX) {
		tmp.info.title_len = PAGE_SLOT_TITLE_MAX;
	}

	s_meta = tmp;
	return STATUS_OK;
}

static status_t page_slot_erase_body(int index)
{
	uint32_t addr = page_slot_addr(index);
	uint32_t first = addr / W25Q_SECTOR_SIZE;
	uint32_t n = PAGE_SLOT_BYTES / W25Q_SECTOR_SIZE;
	uint32_t i;
	w25q_status_t st;

	for (i = 0U; i < n; i++) {
		st = w25q_erase_sector(&g_w25q, first + i);
		if (st != W25Q_OK) {
			printk("page_slot: erase slot %d sec %u fail %d\n", index,
			       (unsigned)(first + i), (int)st);
			return STATUS_FAIL;
		}
	}
	return STATUS_OK;
}

status_t page_slot_init(void)
{
	status_t st;

	if (!w25q_is_initialized(&g_w25q)) {
		printk("page_slot: w25q not ready\n");
		s_ready = false;
		return STATUS_INVALID_STATE;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	st = page_slot_meta_load();
	k_mutex_unlock(&s_meta_lock);

	if (st != STATUS_OK) {
		s_ready = false;
		return STATUS_FAIL;
	}

	s_ready = true;
	printk("page_slot: ready meta=v2 notes=%u info=%u\n",
	       (unsigned)s_meta.notes.content_count, (unsigned)s_meta.info.content_count);
	return STATUS_OK;
}

bool page_slot_is_ready(void)
{
	return s_ready;
}

bool page_slot_mode_page_ok(uint8_t mode, uint8_t page)
{
	return page_slot_index(mode, page) >= 0;
}

bool page_slot_is_content_page(uint8_t mode, uint8_t page)
{
	if (mode == PAGE_SLOT_MODE_HOME) {
		return page == 0U;
	}
	if ((mode == PAGE_SLOT_MODE_NOTES) || (mode == PAGE_SLOT_MODE_INFO)) {
		return (page >= 1U) && (page_slot_index(mode, page) >= 0);
	}
	return false;
}

bool page_slot_is_valid(uint8_t mode, uint8_t page)
{
	int idx;
	bool v;

	if (!s_ready) {
		return false;
	}
	idx = page_slot_index(mode, page);
	if (idx < 0) {
		return false;
	}
	k_mutex_lock(&s_meta_lock, K_FOREVER);
	v = page_slot_bit_get(&s_meta, idx);
	k_mutex_unlock(&s_meta_lock);
	return v;
}

status_t page_slot_store(uint8_t mode, uint8_t page, const uint8_t *bw, const uint8_t *red,
			 uint32_t plane_size)
{
	int idx;
	uint32_t addr;
	w25q_status_t st;
	status_t est;
	page_slot_group_t *grp;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (!page_slot_is_content_page(mode, page)) {
		return STATUS_INVALID_ARG;
	}
	if ((bw == NULL) || (red == NULL)) {
		return STATUS_INVALID_ARG;
	}
	if ((plane_size == 0U) || (plane_size > PAGE_SLOT_PAYLOAD_MAX)) {
		return STATUS_INVALID_ARG;
	}
	if ((plane_size + plane_size) > PAGE_SLOT_BYTES) {
		return STATUS_INVALID_ARG;
	}

	idx = page_slot_index(mode, page);
	if (idx < 0) {
		return STATUS_INVALID_ARG;
	}

	est = page_slot_erase_body(idx);
	if (est != STATUS_OK) {
		return est;
	}

	addr = page_slot_addr(idx);
	st = w25q_write(&g_w25q, addr, bw, plane_size);
	if (st != W25Q_OK) {
		printk("page_slot: store BW fail %d\n", (int)st);
		return STATUS_FAIL;
	}
	st = w25q_write(&g_w25q, addr + plane_size, red, plane_size);
	if (st != W25Q_OK) {
		printk("page_slot: store RED fail %d\n", (int)st);
		return STATUS_FAIL;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	page_slot_bit_set(&s_meta, idx, true);
	grp = page_slot_group_ptr(&s_meta, mode);
	if (grp != NULL) {
		uint8_t need = page; /* content page id == count 上界 */

		if (need > page_slot_content_max(mode)) {
			need = page_slot_content_max(mode);
		}
		if (grp->content_count < need) {
			grp->content_count = need;
		}
	}
	est = page_slot_meta_write_locked(&s_meta);
	k_mutex_unlock(&s_meta_lock);
	return est;
}

status_t page_slot_load(uint8_t mode, uint8_t page, uint8_t *bw, uint8_t *red,
			uint32_t plane_size)
{
	int idx;
	uint32_t addr;
	w25q_status_t st;
	bool valid;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if (!page_slot_is_content_page(mode, page)) {
		return STATUS_INVALID_ARG;
	}
	if ((bw == NULL) || (red == NULL)) {
		return STATUS_INVALID_ARG;
	}
	if ((plane_size == 0U) || (plane_size > PAGE_SLOT_PAYLOAD_MAX)) {
		return STATUS_INVALID_ARG;
	}

	idx = page_slot_index(mode, page);
	if (idx < 0) {
		return STATUS_INVALID_ARG;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	valid = page_slot_bit_get(&s_meta, idx);
	k_mutex_unlock(&s_meta_lock);

	if (!valid) {
		memset(bw, 0xFF, plane_size);
		memset(red, 0x00, plane_size);
		return STATUS_OK;
	}

	addr = page_slot_addr(idx);
	st = w25q_read(&g_w25q, addr, bw, plane_size);
	if (st != W25Q_OK) {
		printk("page_slot: load BW fail %d\n", (int)st);
		return STATUS_FAIL;
	}
	st = w25q_read(&g_w25q, addr + plane_size, red, plane_size);
	if (st != W25Q_OK) {
		printk("page_slot: load RED fail %d\n", (int)st);
		return STATUS_FAIL;
	}

	return STATUS_OK;
}

uint8_t page_slot_group_content_count(uint8_t mode)
{
	uint8_t n = 0U;

	if (!s_ready) {
		return 0U;
	}
	if (mode == PAGE_SLOT_MODE_HOME) {
		return PAGE_SLOT_HOME_COUNT;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	if (mode == PAGE_SLOT_MODE_NOTES) {
		n = s_meta.notes.content_count;
	} else if (mode == PAGE_SLOT_MODE_INFO) {
		n = s_meta.info.content_count;
	}
	k_mutex_unlock(&s_meta_lock);
	return n;
}

status_t page_slot_group_set(uint8_t mode, uint8_t content_count, const uint8_t *title,
			     uint8_t title_len)
{
	page_slot_group_t *grp;
	status_t st;
	uint8_t max_c;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	max_c = page_slot_content_max(mode);
	if ((mode != PAGE_SLOT_MODE_NOTES) && (mode != PAGE_SLOT_MODE_INFO)) {
		return STATUS_INVALID_ARG;
	}
	if (content_count > max_c) {
		return STATUS_INVALID_ARG;
	}
	if (title_len > PAGE_SLOT_TITLE_MAX) {
		return STATUS_INVALID_ARG;
	}
	if ((title_len > 0U) && (title == NULL)) {
		return STATUS_INVALID_ARG;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	grp = page_slot_group_ptr(&s_meta, mode);
	grp->content_count = content_count;
	grp->title_len = title_len;
	memset(grp->title, 0, sizeof(grp->title));
	if (title_len > 0U) {
		memcpy(grp->title, title, title_len);
	}
	/* 缩页：清掉 > count 的内容页 valid */
	{
		uint8_t p;
		uint8_t lim = page_slot_content_max(mode);

		for (p = content_count + 1U; p <= lim; p++) {
			int idx = page_slot_index(mode, p);

			if (idx >= 0) {
				page_slot_bit_set(&s_meta, idx, false);
			}
		}
	}
	st = page_slot_meta_write_locked(&s_meta);
	k_mutex_unlock(&s_meta_lock);

	if (st == STATUS_OK) {
		printk("page_slot: group mode=%u count=%u title_len=%u\n", (unsigned)mode,
		       (unsigned)content_count, (unsigned)title_len);
	}
	return st;
}

status_t page_slot_group_get(uint8_t mode, uint8_t *content_count, uint8_t *title_buf,
			     uint8_t *out_len)
{
	page_slot_group_t *grp;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if ((content_count == NULL) || (title_buf == NULL) || (out_len == NULL)) {
		return STATUS_INVALID_ARG;
	}
	if ((mode != PAGE_SLOT_MODE_NOTES) && (mode != PAGE_SLOT_MODE_INFO)) {
		return STATUS_INVALID_ARG;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	grp = page_slot_group_ptr(&s_meta, mode);
	*content_count = grp->content_count;
	*out_len = grp->title_len;
	memset(title_buf, 0, PAGE_SLOT_TITLE_BUF);
	if (grp->title_len > 0U) {
		memcpy(title_buf, grp->title, grp->title_len);
	}
	k_mutex_unlock(&s_meta_lock);
	return STATUS_OK;
}

uint8_t page_slot_notes_valid_bits(void)
{
	uint8_t bits = 0U;
	uint8_t p;

	if (!s_ready) {
		return 0U;
	}
	k_mutex_lock(&s_meta_lock, K_FOREVER);
	for (p = 1U; p <= PAGE_SLOT_NOTES_CONTENT_MAX; p++) {
		int idx = page_slot_index(PAGE_SLOT_MODE_NOTES, p);

		if ((idx >= 0) && page_slot_bit_get(&s_meta, idx)) {
			bits |= (uint8_t)BIT(p - 1U);
		}
	}
	k_mutex_unlock(&s_meta_lock);
	return bits;
}

uint8_t page_slot_info_valid_bits(void)
{
	uint8_t bits = 0U;
	uint8_t p;

	if (!s_ready) {
		return 0U;
	}
	k_mutex_lock(&s_meta_lock, K_FOREVER);
	for (p = 1U; p <= PAGE_SLOT_INFO_CONTENT_MAX; p++) {
		int idx = page_slot_index(PAGE_SLOT_MODE_INFO, p);

		if ((idx >= 0) && page_slot_bit_get(&s_meta, idx)) {
			bits |= (uint8_t)BIT(p - 1U);
		}
	}
	k_mutex_unlock(&s_meta_lock);
	return bits;
}

bool page_slot_home_valid(void)
{
	return page_slot_is_valid(PAGE_SLOT_MODE_HOME, 0U);
}

status_t page_slot_home_overlay_set(uint8_t flags)
{
	status_t st;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	flags &= (uint8_t)(PAGE_SLOT_OVL_PAGES | PAGE_SLOT_OVL_BATT);

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	s_meta.reserved0[0] = flags;
	st = page_slot_meta_write_locked(&s_meta);
	k_mutex_unlock(&s_meta_lock);

	if (st == STATUS_OK) {
		printk("page_slot: overlay flags=0x%02x\n", (unsigned)flags);
	}
	return st;
}

uint8_t page_slot_home_overlay_get(void)
{
	uint8_t f = 0U;

	if (!s_ready) {
		return 0U;
	}
	k_mutex_lock(&s_meta_lock, K_FOREVER);
	f = s_meta.reserved0[0] & (uint8_t)(PAGE_SLOT_OVL_PAGES | PAGE_SLOT_OVL_BATT);
	k_mutex_unlock(&s_meta_lock);
	return f;
}

status_t page_slot_group_delete_last(uint8_t mode)
{
	page_slot_group_t *grp;
	status_t st;
	uint8_t last;
	int idx;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
	}
	if ((mode != PAGE_SLOT_MODE_NOTES) && (mode != PAGE_SLOT_MODE_INFO)) {
		return STATUS_INVALID_ARG;
	}

	k_mutex_lock(&s_meta_lock, K_FOREVER);
	grp = page_slot_group_ptr(&s_meta, mode);
	if (grp->content_count == 0U) {
		k_mutex_unlock(&s_meta_lock);
		return STATUS_INVALID_ARG;
	}
	last = grp->content_count;
	idx = page_slot_index(mode, last);
	if (idx >= 0) {
		page_slot_bit_set(&s_meta, idx, false);
	}
	grp->content_count = (uint8_t)(last - 1U);
	st = page_slot_meta_write_locked(&s_meta);
	k_mutex_unlock(&s_meta_lock);

	if (st == STATUS_OK) {
		printk("page_slot: delete_last mode=%u was=%u now=%u\n", (unsigned)mode,
		       (unsigned)last, (unsigned)(last - 1U));
	}
	return st;
}

uint32_t page_slot_font_base(void)
{
	return PAGE_SLOT_FONT_BASE;
}

uint32_t page_slot_font_size(void)
{
	return PAGE_SLOT_FONT_SIZE;
}
