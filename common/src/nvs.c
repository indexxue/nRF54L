/**
 * @file nvs.c
 * @brief Zephyr Settings 后端的应用层 NVS（N2 键集）
 */

#include "nvs.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>

#include "fw_version.h"
#include "device_profile.h"
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

typedef struct {
	uint8_t mac[NVS_MAC_SIZE];
	char sn[NVS_SN_SIZE];
	uint8_t device_type;
	uint32_t device_id;
	uint32_t hardware_id;
	uint8_t boot_mode;
	uint8_t have_mac;
	uint8_t have_sn;
	uint8_t have_dtype;
	uint8_t have_did;
	uint8_t have_hid;
	uint8_t have_bmode;
} nvs_cache_t;

static nvs_cache_t s_cache;
static bool s_ready;

static void nvs_cache_apply_defaults(void)
{
	static const uint8_t def_mac[NVS_MAC_SIZE] = NVS_DEFAULT_MAC;

	if (!s_cache.have_mac) {
		memcpy(s_cache.mac, def_mac, NVS_MAC_SIZE);
	}
	if (!s_cache.have_sn) {
		strncpy(s_cache.sn, NVS_DEFAULT_SN, NVS_SN_SIZE - 1U);
		s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
	}
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
		nvs_log_boot_dump();
		return;
	}

	rc = settings_load();
	if (rc != 0) {
		LOG_WARN("settings_load rc=%d (using defaults)", rc);
	}

	nvs_cache_apply_defaults();
	s_ready = true;

	nvs_log_boot_dump();
}

void nvs_log_boot_dump(void)
{
	uint8_t mac[NVS_MAC_SIZE];
	char sn[NVS_SN_SIZE];
	char hw_ver[NVS_HW_VERSION_SIZE];
	char app_ver[NVS_APP_VERSION_SIZE];
	char build[NVS_BUILD_DATE_SIZE];
	uint32_t did;
	uint32_t hid;
	uint32_t active;
	const char *proj;

	if (!s_ready) {
		LOG_WARN("nvs dump: not ready");
		return;
	}

	(void)nvs_mac_get(mac);
	(void)nvs_sn_get(sn);
	(void)nvs_hw_version_get(hw_ver);
	(void)nvs_app_version_get(app_ver);
	did = nvs_device_id_get();
	hid = nvs_hardware_id_get();
	active = nvs_product_id_active();
	proj = nvs_device_id_project_name(active);
	(void)nvs_build_date_format(nvs_build_date_get(), build, sizeof(build));

	LOG_INFO("nvs dump ----");
	LOG_INFO("  ns=%s ready=%u", NVS_APP_NAMESPACE, (unsigned)s_ready);
	LOG_INFO("  mac=%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X have=%u", mac[0], mac[1], mac[2],
		 mac[3], mac[4], mac[5], mac[6], mac[7], (unsigned)s_cache.have_mac);
	LOG_INFO("  sn=%s have=%u", sn, (unsigned)s_cache.have_sn);
	LOG_INFO("  dtype=%u did=0x%08x (nvm) active=0x%08x (%s) have_did=%u",
		 (unsigned)nvs_device_type_get(), did, active, (proj != NULL) ? proj : "?",
		 (unsigned)s_cache.have_did);
	LOG_INFO("  hid=0x%08x (%s) have=%u", hid, device_profile_hardware_name(hid),
		 (unsigned)s_cache.have_hid);
	LOG_INFO("  bmode=%u have=%u  hw=%s app=%s build=%s", (unsigned)nvs_boot_mode_get(),
		 (unsigned)s_cache.have_bmode, hw_ver, app_ver, build);
	LOG_INFO("nvs dump ----");
}

void nvs_factory_reset(void)
{
	static const uint8_t def_mac[NVS_MAC_SIZE] = NVS_DEFAULT_MAC;

	if (!s_ready) {
		nvs_init();
	}

	memcpy(s_cache.mac, def_mac, NVS_MAC_SIZE);
	strncpy(s_cache.sn, NVS_DEFAULT_SN, NVS_SN_SIZE - 1U);
	s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
	s_cache.device_type = NVS_DEFAULT_DEVICE_TYPE;
	s_cache.device_id = NVS_DEVICE_ID_DEFAULT;
	s_cache.hardware_id = NVS_DEFAULT_HARDWARE_ID;
	s_cache.boot_mode = NVS_BOOT_MODE_APP;
	s_cache.have_mac = s_cache.have_sn = s_cache.have_dtype = 1U;
	s_cache.have_did = s_cache.have_hid = s_cache.have_bmode = 1U;

	(void)nvs_save_one(NVS_KEY_MAC, s_cache.mac, NVS_MAC_SIZE);
	(void)nvs_save_one(NVS_KEY_SN, s_cache.sn, strlen(s_cache.sn) + 1U);
	(void)nvs_save_one(NVS_KEY_DEVICE_TYPE, &s_cache.device_type, sizeof(s_cache.device_type));
	(void)nvs_save_one(NVS_KEY_DEVICE_ID, &s_cache.device_id, sizeof(s_cache.device_id));
	(void)nvs_save_one(NVS_KEY_HARDWARE_ID, &s_cache.hardware_id, sizeof(s_cache.hardware_id));
	(void)nvs_save_one(NVS_KEY_BOOT_MODE, &s_cache.boot_mode, sizeof(s_cache.boot_mode));

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
	if ((sn == NULL) || (sn[0] == '\0')) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	strncpy(s_cache.sn, sn, NVS_SN_SIZE - 1U);
	s_cache.sn[NVS_SN_SIZE - 1U] = '\0';
	s_cache.have_sn = 1U;
	return nvs_save_one(NVS_KEY_SN, s_cache.sn, strlen(s_cache.sn) + 1U);
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
	strncpy(version, version_get_string(), NVS_APP_VERSION_SIZE - 1U);
	version[NVS_APP_VERSION_SIZE - 1U] = '\0';
	return true;
}

bool nvs_factory_version_get(char *version)
{
	return nvs_app_version_get(version);
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

uint32_t nvs_firmware_build_date(void)
{
	return NVS_FIRMWARE_BUILD_DATE;
}

uint32_t nvs_build_date_get(void)
{
	return nvs_firmware_build_date();
}

bool nvs_build_date_format(uint32_t ymd, char *out, size_t out_cap)
{
	if ((out == NULL) || (out_cap < NVS_BUILD_DATE_SIZE)) {
		return false;
	}
	(void)snprintk(out, out_cap, "%08u", (unsigned)ymd);
	return true;
}

bool nvs_boot_mode_set(uint8_t mode)
{
	if ((mode != NVS_BOOT_MODE_APP) && (mode != NVS_BOOT_MODE_FACTORY)) {
		return false;
	}
	if (!s_ready) {
		nvs_init();
	}
	s_cache.boot_mode = mode;
	s_cache.have_bmode = 1U;
	return nvs_save_one(NVS_KEY_BOOT_MODE, &s_cache.boot_mode, sizeof(s_cache.boot_mode));
}

uint8_t nvs_boot_mode_get(void)
{
	if (!s_ready) {
		nvs_init();
	}
	return s_cache.boot_mode;
}
