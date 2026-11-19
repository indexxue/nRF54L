/**
 * @file device_profile.c
 * @brief product_id → SKU 名；与 nvs.h NVS_PROJECT_ID_* / 各工程 prj.conf 对齐。
 */

#include "device_profile.h"

#include <stddef.h>

#include <zephyr/sys/util.h>

static const device_product_profile_t s_product_profiles[] = {
	{.product_id = NVS_DEVICE_ID_DEFAULT, .name = "factory"},
	{.product_id = NVS_PROJECT_ID_DEMO, .name = "demo"},
	{.product_id = NVS_PROJECT_ID_EINK_TAG, .name = "eink_tag"},
	{.product_id = NVS_PROJECT_ID_INK_PANEL, .name = "ink_panel"},
	{.product_id = NVS_PROJECT_ID_ZIGBEE_ED, .name = "zigbee_ed"},
	{.product_id = NVS_PROJECT_ID_ZIGBEE_ROUTER, .name = "zigbee_router"},
};

static const device_product_profile_t *s_fallback = &s_product_profiles[0];

static const device_product_profile_t *device_profile_find(uint32_t product_id)
{
	for (size_t i = 0; i < ARRAY_SIZE(s_product_profiles); i++) {
		if (s_product_profiles[i].product_id == product_id) {
			return &s_product_profiles[i];
		}
	}
	return s_fallback;
}

const device_product_profile_t *device_profile_product(void)
{
	return device_profile_find(nvs_product_id_active());
}

uint32_t device_profile_hardware_id(void)
{
	return nvs_hardware_id_get();
}

const char *device_profile_hardware_name(uint32_t hardware_id)
{
	switch (hardware_id) {
	case NVS_HARDWARE_ID_NRF54L15_DK:
		return "nrf54l15_dk";
	case NVS_HARDWARE_ID_NRF54L15_REV_A:
		return "nrf54l15_rev_a";
	default:
		return "unknown";
	}
}
