/**
 * @file nvs.c
 * @brief Settings 业务壳（非自研 NVS）：ty_app 键 + RAM 缓存 + boot_share
 */

#include "nvs.h"

#include <stddef.h>
#include <string.h>

#include <zephyr/drivers/hwinfo.h>
#include <zephyr/kernel.h>
#include <zephyr/random/random.h>
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

#include <pm_config.h>

#include <zephyr/sys/crc.h>

#include "boot_share.h"
#include "fw_version.h"
#include "log.h"

#ifndef NVS_FIRMWARE_BUILD_DATE
#define NVS_BUILD_MONTH_IS(c0, c1, c2) (__DATE__[0] == (c0) && __DATE__[1] == (c1) && __DATE__[2] == (c2))
#define NVS_BUILD_MONTH_NUM                                                                            \
	(NVS_BUILD_MONTH_IS('J', 'a', 'n')   ? 1U                                                      \
	 : NVS_BUILD_MONTH_IS('F', 'e', 'b') ? 2U                                                      \
	 : NVS_BUILD_MONTH_IS('M', 'a', 'r') ? 3U                                                      \
	 : NVS_BUILD_MONTH_IS('A', 'p', 'r') ? 4U                                                      \
	 : NVS_BUILD_MONTH_IS('M', 'a', 'y') ? 5U                                                      \
	 : NVS_BUILD_MONTH_IS('J', 'u', 'n') ? 6U                                                      \
	 : NVS_BUILD_MONTH_IS('J', 'u', 'l') ? 7U                                                      \
	 : NVS_BUILD_MONTH_IS('A', 'u', 'g') ? 8U                                                      \
	 : NVS_BUILD_MONTH_IS('S', 'e', 'p') ? 9U                                                      \
	 : NVS_BUILD_MONTH_IS('O', 'c', 't') ? 10U                                                     \
	 : NVS_BUILD_MONTH_IS('N', 'o', 'v') ? 11U                                                     \
					       : 12U)
#define NVS_BUILD_DAY_NUM                                                                              \
	((uint32_t)((__DATE__[4] == ' ' ? 0U : (uint32_t)(__DATE__[4] - '0')) * 10U +                  \
		    (uint32_t)(__DATE__[5] - '0')))
#define NVS_BUILD_YEAR_NUM                                                                             \
	((uint32_t)(__DATE__[7] - '0') * 1000U + (uint32_t)(__DATE__[8] - '0') * 100U +                \
	 (uint32_t)(__DATE__[9] - '0') * 10U + (uint32_t)(__DATE__[10] - '0'))
#define NVS_FIRMWARE_BUILD_DATE                                                                        \
	(NVS_BUILD_YEAR_NUM * 10000U + NVS_BUILD_MONTH_NUM * 100U + NVS_BUILD_DAY_NUM)
#endif

#define NVS_KEY_MAC "mac"
#define NVS_KEY_SN "sn"
#define NVS_KEY_DEVICE_TYPE "dtype"
#define NVS_KEY_DEVICE_ID "did"
#define NVS_KEY_HARDWARE_ID "hid"
#define NVS_KEY_BOOT_MODE "bmode"
#define NVS_KEY_EPD_PANEL "epanel"

typedef struct {
	uint8_t mac[NVS_MAC_SIZE];
	char sn[NVS_SN_SIZE];
	uint8_t device_type;
	uint32_t device_id;
	uint32_t hardware_id;
	uint8_t boot_mode;
	uint8_t epd_panel;
	uint8_t have_mac;
	uint8_t have_sn;
	uint8_t have_dtype;
	uint8_t have_did;
	uint8_t have_hid;
	uint8_t have_bmode;
	uint8_t have_epanel;
} nvs_cache_t;

static nvs_cache_t s_cache;
static bool s_ready;

static bool nvs_boot_share_persist(uint8_t mode);

static void nvs_cache_apply_defaults(void)
{
	/* mac/sn：无键时不在此填默认；nvs_init 末尾生成并落盘 */
	if (!s_cache.have_dtype) {
		s_cache.device_type = NVS_DEFAULT_DEVICE_TYPE;
	}
	if (!s_cache.have_did) {
		s_cache.device_id = NVS_DEVICE_ID_DEFAULT;
	}
	if (!s_cache.have_hid) {
		s_cache.hardware_id = NVS_DEFAULT_HARDWARE_ID;
	}
	if (!s_cache.have_bmode) {
		s_cache.boot_mode = NVS_BOOT_MODE_APP;
	}
	if (!s_cache.have_epanel) {
		s_cache.epd_panel = NVS_EPD_PANEL_2IN9;
	}
}

static int nvs_settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg)
{
	const char *next = NULL;
	int rc;

	if (settings_name_steq(name, NVS_KEY_MAC, &next) && (next == NULL)) {
		if (len != NVS_MAC_SIZE) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, s_cache.mac, len);
		if (rc >= 0) {
			s_cache.have_mac = 1U;
		}
		return rc;
	}
	if (settings_name_steq(name, NVS_KEY_SN, &next) && (next == NULL)) {
		if (len > NVS_SN_SIZE) {
			return -EINVAL;
		}
		memset(s_cache.sn, 0, sizeof(s_cache.sn));
		rc = read_cb(cb_arg, s_cache.sn, len);
		if (rc >= 0) {
			s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
			s_cache.have_sn = 1U;
		}
		return rc;
	}
	if (settings_name_steq(name, NVS_KEY_DEVICE_TYPE, &next) && (next == NULL)) {
		if (len != sizeof(s_cache.device_type)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &s_cache.device_type, len);
		if (rc >= 0) {
			s_cache.have_dtype = 1U;
		}
		return rc;
	}
	if (settings_name_steq(name, NVS_KEY_DEVICE_ID, &next) && (next == NULL)) {
		if (len != sizeof(s_cache.device_id)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &s_cache.device_id, len);
		if (rc >= 0) {
			s_cache.have_did = 1U;
		}
		return rc;
	}
	if (settings_name_steq(name, NVS_KEY_HARDWARE_ID, &next) && (next == NULL)) {
		if (len != sizeof(s_cache.hardware_id)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &s_cache.hardware_id, len);
		if (rc >= 0) {
			s_cache.have_hid = 1U;
		}
		return rc;
	}
	if (settings_name_steq(name, NVS_KEY_BOOT_MODE, &next) && (next == NULL)) {
		if (len != sizeof(s_cache.boot_mode)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &s_cache.boot_mode, len);
		if (rc >= 0) {
			s_cache.have_bmode = 1U;
		}
		return rc;
	}
	if (settings_name_steq(name, NVS_KEY_EPD_PANEL, &next) && (next == NULL)) {
		if (len != sizeof(s_cache.epd_panel)) {
			return -EINVAL;
		}
		rc = read_cb(cb_arg, &s_cache.epd_panel, len);
		if (rc >= 0) {
			s_cache.have_epanel = 1U;
		}
		return rc;
	}

	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(ty_app, NVS_APP_NAMESPACE, NULL, nvs_settings_set, NULL, NULL);

static bool nvs_save_one(const char *key, const void *data, size_t len)
{
	char path[40];
	int rc;

	if (!s_ready) {
		return false;
	}
	rc = snprintk(path, sizeof(path), "%s/%s", NVS_APP_NAMESPACE, key);
	if ((rc < 0) || (rc >= (int)sizeof(path))) {
		return false;
	}
	rc = settings_save_one(path, data, len);
	if (rc != 0) {
		LOG_WARN("nvs save %s rc=%d", key, rc);
		return false;
	}
	return true;
}

/** T90M + 8 hex；优先 CSPRNG，失败则 hwinfo⊕cycle */
static bool nvs_sn_generate_and_persist(void)
{
	static const char hex[] = "0123456789ABCDEF";
	uint8_t rnd[4];
	char sn[NVS_SN_SIZE];
	uint32_t mix;
	ssize_t id_len;
	uint8_t id[8];
	int i;

	BUILD_ASSERT((NVS_SN_PREFIX_LEN + NVS_SN_RAND_HEX_LEN + 1U) <= NVS_SN_SIZE,
		     "SN prefix+rand must fit NVS_SN_SIZE");

	if (sys_csrand_get(rnd, sizeof(rnd)) != 0) {
		mix = k_cycle_get_32();
		memset(id, 0, sizeof(id));
		id_len = hwinfo_get_device_id(id, sizeof(id));
		for (i = 0; i < (int)sizeof(rnd); i++) {
			uint8_t b = (uint8_t)(mix >> (8 * i));

			if ((id_len > 0) && (i < (int)id_len)) {
				b ^= id[i];
			}
			rnd[i] = b;
		}
		LOG_WARN("nvs: csrand fail, SN from hwinfo/cycle");
	}

	memcpy(sn, NVS_SN_PREFIX, NVS_SN_PREFIX_LEN);
	for (i = 0; i < 4; i++) {
		sn[NVS_SN_PREFIX_LEN + (i * 2)] = hex[(rnd[i] >> 4) & 0x0FU];
		sn[NVS_SN_PREFIX_LEN + (i * 2) + 1] = hex[rnd[i] & 0x0FU];
	}
	sn[NVS_SN_PREFIX_LEN + NVS_SN_RAND_HEX_LEN] = '\0';

	strncpy(s_cache.sn, sn, NVS_SN_SIZE - 1U);
	s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
	s_cache.have_sn = 1U;

	if (!nvs_save_one(NVS_KEY_SN, s_cache.sn, strlen(s_cache.sn) + 1U)) {
		LOG_WARN("nvs: SN generate ok in RAM, save failed sn=%s", s_cache.sn);
		return false;
	}

	LOG_INFO("nvs: first-boot SN generated sn=%s", s_cache.sn);
	return true;
}

/**
 * 首启 MAC：优先 FICR device_id（与 Zigbee IEEE 同源唯一段），失败则随机。
 * 写入后不再改；可用串口 mac 命令覆盖。
 */
static bool nvs_mac_generate_and_persist(void)
{
	uint8_t mac[NVS_MAC_SIZE];
	ssize_t id_len;
	int i;
	bool from_id = false;

	memset(mac, 0, sizeof(mac));
	id_len = hwinfo_get_device_id(mac, sizeof(mac));
	if (id_len > 0) {
		for (i = 0; i < (int)id_len; i++) {
			if (mac[i] != 0U) {
				from_id = true;
				break;
			}
		}
	}
	if (!from_id) {
		if (sys_csrand_get(mac, sizeof(mac)) != 0) {
			(void)sys_rand_get(mac, sizeof(mac));
		}
		/* 本地管理位，避免与全球 OUI 冲突 */
		mac[0] = (uint8_t)((mac[0] & 0xFEU) | 0x02U);
		LOG_WARN("nvs: MAC from random (no device_id)");
	}

	memcpy(s_cache.mac, mac, NVS_MAC_SIZE);
	s_cache.have_mac = 1U;
	if (!nvs_save_one(NVS_KEY_MAC, s_cache.mac, NVS_MAC_SIZE)) {
		LOG_WARN("nvs: MAC generate ok in RAM, save failed");
		return false;
	}
	LOG_INFO("nvs: first-boot MAC %s mac=%02X%02X%02X%02X%02X%02X%02X%02X",
		 from_id ? "device_id" : "random", mac[0], mac[1], mac[2], mac[3], mac[4],
		 mac[5], mac[6], mac[7]);
	return true;
}

void nvs_init(void)
{
	int rc;

	if (s_ready) {
		return;
	}

	memset(&s_cache, 0, sizeof(s_cache));

	rc = settings_subsys_init();
	if (rc != 0) {
		LOG_ERROR("settings_subsys_init rc=%d", rc);
		nvs_cache_apply_defaults();
		s_ready = true;
		if (!s_cache.have_mac) {
			(void)nvs_mac_generate_and_persist();
		}
		if (!s_cache.have_sn) {
			(void)nvs_sn_generate_and_persist();
		}
		nvs_log_boot_dump();
		return;
	}

	rc = settings_load();
	if (rc != 0) {
		LOG_WARN("settings_load rc=%d (using defaults)", rc);
	}

	nvs_cache_apply_defaults();
	s_ready = true;

	if (!s_cache.have_mac) {
		if (!nvs_mac_generate_and_persist()) {
			static const uint8_t def_mac[NVS_MAC_SIZE] = NVS_DEFAULT_MAC;

			memcpy(s_cache.mac, def_mac, NVS_MAC_SIZE);
			s_cache.have_mac = 1U;
		}
	}

	if (!s_cache.have_sn) {
		if (!nvs_sn_generate_and_persist()) {
			strncpy(s_cache.sn, NVS_DEFAULT_SN, NVS_SN_SIZE - 1U);
			s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
			s_cache.have_sn = 1U;
		}
	}

	nvs_log_boot_dump();
}

void nvs_log_boot_dump(void)
{
	uint8_t mac[NVS_MAC_SIZE];
	char sn[NVS_SN_SIZE];
	char app_ver[NVS_APP_VERSION_SIZE];
	char build[NVS_BUILD_DATE_SIZE];

	if (!s_ready) {
		LOG_WARN("nvs dump: not ready");
		return;
	}

	(void)nvs_mac_get(mac);
	(void)nvs_sn_get(sn);
	(void)nvs_app_version_get(app_ver);
	(void)nvs_build_date_format(nvs_build_date_get(), build, sizeof(build));

	LOG_INFO("nvs: sn=%s mac=%02X%02X%02X%02X%02X%02X%02X%02X bmode=%u app=%s build=%s", sn,
		 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], mac[6], mac[7],
		 (unsigned)nvs_boot_mode_get(), app_ver, build);
}

void nvs_factory_reset(void)
{
	if (!s_ready) {
		nvs_init();
	}

	s_cache.device_type = NVS_DEFAULT_DEVICE_TYPE;
	s_cache.device_id = NVS_DEVICE_ID_DEFAULT;
	s_cache.hardware_id = NVS_DEFAULT_HARDWARE_ID;
	s_cache.boot_mode = NVS_BOOT_MODE_APP;
	s_cache.epd_panel = NVS_EPD_PANEL_2IN9;
	s_cache.have_dtype = 1U;
	s_cache.have_did = s_cache.have_hid = s_cache.have_bmode = 1U;
	s_cache.have_epanel = 1U;
	s_cache.have_mac = 0U;
	s_cache.have_sn = 0U;

	(void)nvs_save_one(NVS_KEY_DEVICE_TYPE, &s_cache.device_type, sizeof(s_cache.device_type));
	(void)nvs_save_one(NVS_KEY_DEVICE_ID, &s_cache.device_id, sizeof(s_cache.device_id));
	(void)nvs_save_one(NVS_KEY_HARDWARE_ID, &s_cache.hardware_id, sizeof(s_cache.hardware_id));
	(void)nvs_save_one(NVS_KEY_BOOT_MODE, &s_cache.boot_mode, sizeof(s_cache.boot_mode));
	(void)nvs_save_one(NVS_KEY_EPD_PANEL, &s_cache.epd_panel, sizeof(s_cache.epd_panel));
	(void)nvs_boot_share_persist(s_cache.boot_mode);

	if (!nvs_mac_generate_and_persist()) {
		static const uint8_t def_mac[NVS_MAC_SIZE] = NVS_DEFAULT_MAC;

		memcpy(s_cache.mac, def_mac, NVS_MAC_SIZE);
		s_cache.have_mac = 1U;
		(void)nvs_save_one(NVS_KEY_MAC, s_cache.mac, NVS_MAC_SIZE);
	}

	if (!nvs_sn_generate_and_persist()) {
		strncpy(s_cache.sn, NVS_DEFAULT_SN, NVS_SN_SIZE - 1U);
		s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
		s_cache.have_sn = 1U;
		(void)nvs_save_one(NVS_KEY_SN, s_cache.sn, strlen(s_cache.sn) + 1U);
	}

	LOG_INFO("nvs factory_reset done");
}

bool nvs_mac_set(const uint8_t *mac)
{
	if (mac == NULL) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	memcpy(s_cache.mac, mac, NVS_MAC_SIZE);
	s_cache.have_mac = 1U;
	return nvs_save_one(NVS_KEY_MAC, s_cache.mac, NVS_MAC_SIZE);
}

bool nvs_mac_get(uint8_t *mac)
{
	if (mac == NULL) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	memcpy(mac, s_cache.mac, NVS_MAC_SIZE);
	return true;
}

bool nvs_sn_set(const char *sn)
{
	size_t len;

	if ((sn == NULL) || (sn[0] == '\0')) {
		return false;
	}
	len = strlen(sn);
	if (len >= NVS_SN_SIZE) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	memcpy(s_cache.sn, sn, len);
	s_cache.sn[len] = '\0';
	s_cache.have_sn = 1U;
	return nvs_save_one(NVS_KEY_SN, s_cache.sn, len + 1U);
}

bool nvs_sn_get(char *sn)
{
	if (sn == NULL) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	memcpy(sn, s_cache.sn, NVS_SN_SIZE);
	return true;
}

bool nvs_hw_version_get(char *version)
{
	if (version == NULL) {
		return false;
	}
	strncpy(version, NVS_HW_VERSION_STRING, NVS_HW_VERSION_SIZE - 1U);
	version[NVS_HW_VERSION_SIZE - 1U] = '\0';
	return true;
}

bool nvs_app_version_get(char *version)
{
	if (version == NULL) {
		return false;
	}
	strncpy(version, fw_version_get_string(), NVS_APP_VERSION_SIZE - 1U);
	version[NVS_APP_VERSION_SIZE - 1U] = '\0';
	return true;
}

void nvs_device_type_set(uint8_t type)
{
	if (!s_ready) {
		nvs_init();
	}
	s_cache.device_type = type;
	s_cache.have_dtype = 1U;
	(void)nvs_save_one(NVS_KEY_DEVICE_TYPE, &s_cache.device_type, sizeof(s_cache.device_type));
}

uint8_t nvs_device_type_get(void)
{
	if (!s_ready) {
		nvs_init();
	}
	return s_cache.device_type;
}

bool nvs_device_id_set(uint32_t id)
{
	if (!s_ready) {
		nvs_init();
	}
	s_cache.device_id = id;
	s_cache.have_did = 1U;
	return nvs_save_one(NVS_KEY_DEVICE_ID, &s_cache.device_id, sizeof(s_cache.device_id));
}

uint32_t nvs_device_id_get(void)
{
	if (!s_ready) {
		nvs_init();
	}
	return s_cache.device_id;
}

bool nvs_hardware_id_set(uint32_t id)
{
	if (!s_ready) {
		nvs_init();
	}
	s_cache.hardware_id = id;
	s_cache.have_hid = 1U;
	return nvs_save_one(NVS_KEY_HARDWARE_ID, &s_cache.hardware_id, sizeof(s_cache.hardware_id));
}

uint32_t nvs_hardware_id_get(void)
{
	if (!s_ready) {
		nvs_init();
	}
	return s_cache.hardware_id;
}

uint32_t nvs_product_id_active(void)
{
	if (NVS_DEFAULT_DEVICE_ID != NVS_DEVICE_ID_DEFAULT) {
		return (uint32_t)NVS_DEFAULT_DEVICE_ID;
	}
	return nvs_device_id_get();
}

const char *nvs_device_id_project_name(uint32_t id)
{
	if (id == NVS_DEVICE_ID_DEFAULT) {
		return "factory";
	}
#define NVS_NAME_ROW(sym, val, str)                                                                                    \
	if (id == (val)) {                                                                                             \
		return (str);                                                                                          \
	}
	NVS_PROJECT_ID_TABLE(NVS_NAME_ROW)
#undef NVS_NAME_ROW
	return "unknown";
}

uint32_t nvs_build_date_get(void)
{
	return NVS_FIRMWARE_BUILD_DATE;
}

bool nvs_build_date_format(uint32_t ymd, char *out, size_t out_cap)
{
	if ((out == NULL) || (out_cap < NVS_BUILD_DATE_SIZE)) {
		return false;
	}
	(void)snprintk(out, out_cap, "%08u", (unsigned)ymd);
	return true;
}

/**
 * Dual-write boot_mode into boot_share page (MCUBoot reads this; no Settings parse).
 * No-op (success) when partition absent (pre-MCUBoot apps).
 */
static bool nvs_boot_share_persist(uint8_t mode)
{
#ifdef PM_BOOT_SHARE_ID
	const struct flash_area *fa;
	struct boot_share_record rec;
	int rc;

	memset(&rec, 0, sizeof(rec));
	rec.magic = BOOT_SHARE_MAGIC;
	rec.version = BOOT_SHARE_VERSION;
	rec.boot_mode = mode;
	rec.crc32 = crc32_ieee((const uint8_t *)&rec,
			       offsetof(struct boot_share_record, crc32));

	rc = flash_area_open(PM_BOOT_SHARE_ID, &fa);
	if (rc != 0) {
		LOG_ERROR("boot_share open fail %d", rc);
		return false;
	}

	rc = flash_area_flatten(fa, 0, fa->fa_size);
	if (rc != 0) {
		LOG_ERROR("boot_share flatten fail %d", rc);
		flash_area_close(fa);
		return false;
	}

	rc = flash_area_write(fa, 0, &rec, sizeof(rec));
	flash_area_close(fa);
	if (rc != 0) {
		LOG_ERROR("boot_share write fail %d", rc);
		return false;
	}

	return true;
#else
	ARG_UNUSED(mode);
	return true;
#endif
}

bool nvs_boot_mode_set(uint8_t mode)
{
	bool ok;

	if ((mode != NVS_BOOT_MODE_APP) && (mode != NVS_BOOT_MODE_FACTORY)) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	s_cache.boot_mode = mode;
	s_cache.have_bmode = 1U;
	ok = nvs_save_one(NVS_KEY_BOOT_MODE, &s_cache.boot_mode, sizeof(s_cache.boot_mode));
	if (!ok) {
		return false;
	}
	if (!nvs_boot_share_persist(mode)) {
		LOG_ERROR("boot_mode Settings ok but boot_share failed");
		return false;
	}
	return true;
}

uint8_t nvs_boot_mode_get(void)
{
	if (!s_ready) {
		nvs_init();
	}
	return s_cache.boot_mode;
}

bool nvs_epd_panel_set(uint8_t panel)
{
	if ((panel != NVS_EPD_PANEL_2IN9) && (panel != NVS_EPD_PANEL_4IN2)) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	s_cache.epd_panel = panel;
	s_cache.have_epanel = 1U;
	return nvs_save_one(NVS_KEY_EPD_PANEL, &s_cache.epd_panel, sizeof(s_cache.epd_panel));
}

uint8_t nvs_epd_panel_get(void)
{
	if (!s_ready) {
		nvs_init();
	}
	if ((s_cache.epd_panel != NVS_EPD_PANEL_2IN9) &&
	    (s_cache.epd_panel != NVS_EPD_PANEL_4IN2)) {
		return NVS_EPD_PANEL_2IN9;
	}
	return s_cache.epd_panel;
}
