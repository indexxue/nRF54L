/**
 * @file device_profile.h
 * @brief 产品档案：product_id → SKU 名；hardware_id → 板名。
 *
 * 表须覆盖全部 NVS_PROJECT_ID_*（见 nvs.h / common/README.md）。
 */

#ifndef COMMON_DEVICE_PROFILE_H
#define COMMON_DEVICE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "nvs.h"

typedef struct {
	uint32_t product_id;
	const char *name;
} device_product_profile_t;

const device_product_profile_t *device_profile_product(void);

uint32_t device_profile_hardware_id(void);
const char *device_profile_hardware_name(uint32_t hardware_id);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_DEVICE_PROFILE_H */
