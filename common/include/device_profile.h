/**
 * @file device_profile.h
 * @brief 产品档案：product_id → 外设初始化掩码（I2C / SPI / BUTTON / 2.4G）
 */

#ifndef COMMON_DEVICE_PROFILE_H
#define COMMON_DEVICE_PROFILE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "nvs.h"
#include "type.h"

/* -------------------------------------------------------------------------- */
/* 板级外设掩码（总线 / 射频）                                                   */
/* -------------------------------------------------------------------------- */

#define DEVICE_BOARD_MASK_I2C        (1U << 0)
#define DEVICE_BOARD_MASK_SPI        (1U << 1)
#define DEVICE_BOARD_MASK_RADIO_2P4G (1U << 2) /* BLE / 私有 2.4G 等 */

#define DEVICE_BOARD_MASK_FULL                                                                         \
	(DEVICE_BOARD_MASK_I2C | DEVICE_BOARD_MASK_SPI | DEVICE_BOARD_MASK_RADIO_2P4G)

/* -------------------------------------------------------------------------- */
/* 平台壳层掩码                                                                */
/* -------------------------------------------------------------------------- */

#define DEVICE_PLATFORM_MASK_BUTTON (1U << 0)

#define DEVICE_PLATFORM_MASK_FULL (DEVICE_PLATFORM_MASK_BUTTON)

/* -------------------------------------------------------------------------- */
/* 按键规格（gpio=-1 表示尚未绑板；button 模块接入后再用）                       */
/* -------------------------------------------------------------------------- */

typedef struct {
	int32_t gpio;
	uint8_t id;
	const char *name;
	uint8_t active_level;
	uint16_t permission;
} device_button_spec_t;

typedef struct {
	uint32_t product_id;
	const char *name;
	uint32_t board_mask;
	uint32_t platform_mask;
	const device_button_spec_t *buttons;
	uint8_t button_count;
} device_product_profile_t;

const device_product_profile_t *device_profile_product(void);

uint32_t device_profile_hardware_id(void);
const char *device_profile_hardware_name(uint32_t hardware_id);

uint32_t device_profile_board_mask(void);
uint32_t device_profile_platform_mask(void);

bool device_profile_board_wants(uint32_t mask);
bool device_profile_platform_wants(uint32_t mask);

uint8_t device_profile_button_count(void);
const device_button_spec_t *device_profile_button_spec(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_DEVICE_PROFILE_H */
