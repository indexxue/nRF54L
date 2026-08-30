/**
 * @file device_profile.c
 * @brief demo / factory 空档案；正式产品在此表追加行。
 */

#include "device_profile.h"

#include <zephyr/sys/util.h>

static const device_product_profile_t s_product_profiles[] = {
	{
		.product_id = NVS_DEVICE_ID_DEFAULT,
		.name = "factory",
		.board_mask = 0U,
		.platform_mask = 0U,
		.buttons = NULL,
		.button_count = 0U,
	},
	{
		.product_id = NVS_PROJECT_ID_DEMO,
		.name = "demo",
		.board_mask = 0U,
		.platform_mask = 0U,
		.buttons = NULL,
		.button_count = 0U,
	},
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

uint32_t device_profile_board_mask(void)
{
	return device_profile_product()->board_mask;
}

uint32_t device_profile_platform_mask(void)
{
	return device_profile_product()->platform_mask;
}

bool device_profile_board_wants(uint32_t mask)
{
	return (device_profile_board_mask() & mask) == mask;
}

bool device_profile_platform_wants(uint32_t mask)
{
	return (device_profile_platform_mask() & mask) == mask;
}

uint8_t device_profile_button_count(void)
{
	return device_profile_product()->button_count;
}

const device_button_spec_t *device_profile_button_spec(uint8_t index)
{
	const device_product_profile_t *p = device_profile_product();

	if ((p->buttons == NULL) || (index >= p->button_count)) {
		return NULL;
	}
	return &p->buttons[index];
}
