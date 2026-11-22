/**
 * @file page_slot.c
 * @brief ink_panel W25Q 页槽读写
 *
 * meta@0x0000 (4KB)
 * slots@0x1000 : home×1 + notes×8 + info×4，各 32KB
 * font_cjk@0x69000 : 2MB 预留（本文件不访问）
 */

#include "page_slot.h"

#include "w25q_board.h"

#include <string.h>

#include <zephyr/sys/printk.h>
#include <zephyr/sys/util.h>

#define PAGE_SLOT_MAGIC   0x31534750U /* 'PGS1' LE */
#define PAGE_SLOT_VER     1U
#define PAGE_SLOT_META_SZ W25Q_SECTOR_SIZE
#define PAGE_SLOT_BASE    PAGE_SLOT_META_SZ
#define PAGE_SLOT_TOTAL \
	(PAGE_SLOT_HOME_COUNT + PAGE_SLOT_NOTES_COUNT + PAGE_SLOT_INFO_COUNT)
#define PAGE_SLOT_FONT_BASE \
	(PAGE_SLOT_BASE + ((uint32_t)PAGE_SLOT_TOTAL * PAGE_SLOT_BYTES))
#define PAGE_SLOT_FONT_SIZE 0x200000U /* 2 MiB */

#define PAGE_SLOT_PAYLOAD_MAX 15000U

typedef struct {
	uint32_t magic;
	uint16_t version;
	uint16_t slot_bytes;
	uint8_t valid_bits[(PAGE_SLOT_TOTAL + 7U) / 8U];
	uint8_t boot_mode;
	uint8_t boot_page;
	uint8_t reserved[16];
} page_slot_meta_t;

static page_slot_meta_t s_meta;
static bool s_ready;

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

static void page_slot_meta_default(page_slot_meta_t *m)
{
	memset(m, 0, sizeof(*m));
	m->magic = PAGE_SLOT_MAGIC;
	m->version = PAGE_SLOT_VER;
	m->slot_bytes = (uint16_t)PAGE_SLOT_BYTES;
	m->boot_mode = PAGE_SLOT_MODE_HOME;
	m->boot_page = 0U;
}

static status_t page_slot_meta_write(const page_slot_meta_t *m)
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
		printk("page_slot: meta invalid -> format\n");
		page_slot_meta_default(&s_meta);
		return page_slot_meta_write(&s_meta);
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
	if (!w25q_is_initialized(&g_w25q)) {
		printk("page_slot: w25q not ready\n");
		s_ready = false;
		return STATUS_INVALID_STATE;
	}

	if (page_slot_meta_load() != STATUS_OK) {
		s_ready = false;
		return STATUS_FAIL;
	}

	s_ready = true;
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

bool page_slot_is_valid(uint8_t mode, uint8_t page)
{
	int idx;

	if (!s_ready) {
		return false;
	}
	idx = page_slot_index(mode, page);
	if (idx < 0) {
		return false;
	}
	return page_slot_bit_get(&s_meta, idx);
}

status_t page_slot_store(uint8_t mode, uint8_t page, const uint8_t *bw, const uint8_t *red,
			 uint32_t plane_size)
{
	int idx;
	uint32_t addr;
	w25q_status_t st;
	status_t est;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
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

	page_slot_bit_set(&s_meta, idx, true);
	est = page_slot_meta_write(&s_meta);
	if (est != STATUS_OK) {
		return est;
	}

	return STATUS_OK;
}

status_t page_slot_load(uint8_t mode, uint8_t page, uint8_t *bw, uint8_t *red,
			uint32_t plane_size)
{
	int idx;
	uint32_t addr;
	w25q_status_t st;

	if (!s_ready) {
		return STATUS_INVALID_STATE;
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

	if (!page_slot_bit_get(&s_meta, idx)) {
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

uint32_t page_slot_font_base(void)
{
	return PAGE_SLOT_FONT_BASE;
}

uint32_t page_slot_font_size(void)
{
	return PAGE_SLOT_FONT_SIZE;
}
