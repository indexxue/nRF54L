/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * boot_go_hook: if boot_share.boot_mode == factory, boot mcuboot_secondary
 * (no swap). Factory image must be Direct-XIP / secondary-linked (see factory
 * SB_CONFIG_MCUBOOT_MODE_DIRECT_XIP + mcuboot_secondary_app).
 */

#include <stddef.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include <pm_config.h>

#include "bootutil/bootutil.h"
#include "bootutil/bootutil_log.h"
#include "bootutil/bootutil_public.h"
#include "bootutil/fault_injection_hardening.h"
#include "bootutil/image.h"

#include <zephyr/sys/crc.h>

#include "boot_share.h"

BOOT_LOG_MODULE_DECLARE(mcuboot);

#define BOOT_TMPBUF_SZ 256

static struct image_header s_hdr;
static uint8_t s_tmpbuf[BOOT_TMPBUF_SZ];

static bool boot_share_read_factory(void)
{
	const struct flash_area *fa;
	struct boot_share_record rec;
	uint32_t expect_crc;
	int rc;

#ifndef PM_BOOT_SHARE_ID
	return false;
#else
	rc = flash_area_open(PM_BOOT_SHARE_ID, &fa);
	if (rc != 0) {
		return false;
	}

	rc = flash_area_read(fa, 0, &rec, sizeof(rec));
	flash_area_close(fa);
	if (rc != 0) {
		return false;
	}

	if ((rec.magic != BOOT_SHARE_MAGIC) || (rec.version != BOOT_SHARE_VERSION)) {
		return false;
	}
	if (rec.boot_mode != BOOT_SHARE_MODE_FACTORY) {
		return false;
	}

	expect_crc = crc32_ieee((const uint8_t *)&rec,
				offsetof(struct boot_share_record, crc32));
	if (expect_crc != rec.crc32) {
		BOOT_LOG_WRN("boot_share CRC mismatch");
		return false;
	}

	return true;
#endif
}

static fih_ret boot_try_secondary(struct boot_rsp *rsp)
{
	const struct flash_area *fa;
	FIH_DECLARE(fih_rc, FIH_FAILURE);
	int rc;

#ifndef PM_MCUBOOT_SECONDARY_ID
	FIH_RET(FIH_BOOT_HOOK_REGULAR);
#else
	rc = flash_area_open(PM_MCUBOOT_SECONDARY_ID, &fa);
	if (rc != 0) {
		BOOT_LOG_ERR("secondary open fail %d", rc);
		FIH_RET(FIH_BOOT_HOOK_REGULAR);
	}

	rc = boot_image_load_header(fa, &s_hdr);
	if (rc != 0) {
		BOOT_LOG_WRN("secondary header invalid (%d), fall back to primary", rc);
		flash_area_close(fa);
		FIH_RET(FIH_BOOT_HOOK_REGULAR);
	}

	FIH_CALL(bootutil_img_validate, fih_rc, NULL, &s_hdr, fa, s_tmpbuf, BOOT_TMPBUF_SZ,
		 NULL, 0, NULL);
	if (FIH_NOT_EQ(fih_rc, FIH_SUCCESS)) {
		BOOT_LOG_WRN("secondary validate fail, fall back to primary");
		flash_area_close(fa);
		FIH_RET(FIH_BOOT_HOOK_REGULAR);
	}

	rsp->br_flash_dev_id = flash_area_get_device_id(fa);
	rsp->br_image_off = flash_area_get_off(fa);
	rsp->br_hdr = &s_hdr;

	flash_area_close(fa);
	BOOT_LOG_INF("boot_mode=factory -> secondary (no swap)");
	FIH_RET(fih_rc);
#endif
}

fih_ret boot_go_hook(struct boot_rsp *rsp)
{
	if (!boot_share_read_factory()) {
		FIH_RET(FIH_BOOT_HOOK_REGULAR);
	}

	return boot_try_secondary(rsp);
}
