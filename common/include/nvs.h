/**
 * @file nvs.h
 * @brief 应用层非易失参数 API（Zephyr Settings → storage 分区）
 *
 * N2 最小常用集：did / hid / mac / sn / dtype / boot_mode + 版本只读。
 * 键落在命名空间 NVS_APP_NAMESPACE（Settings 路径 ty_app/<key>）。
 */

#ifndef COMMON_NVS_H
#define COMMON_NVS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flash_partition.h"

/* -------------------------------------------------------------------------- */
/* 项目 product_id（NVS 键 did；factory 默认 0）                               */
/* -------------------------------------------------------------------------- */

#define NVS_DEVICE_ID_DEFAULT 0U

/** 项目 ID：0x5353PPNN（PP=类别 0x01，NN=序号）— 与 ESP 族编码兼容 */
#define NVS_PROJECT_ID_CAT_PROJECT 0x53530100U
#define NVS_PROJECT_ID_MAKE(index) (NVS_PROJECT_ID_CAT_PROJECT | ((uint32_t)(index) & 0xFFU))

#define NVS_PROJECT_ID_DEMO     NVS_PROJECT_ID_MAKE(0x10U)
#define NVS_PROJECT_ID_EINK_TAG NVS_PROJECT_ID_MAKE(0x20U)

#define NVS_PROJECT_ID_TABLE(X)                         \
	X(DEMO, NVS_PROJECT_ID_DEMO, "demo")            \
	X(EINK_TAG, NVS_PROJECT_ID_EINK_TAG, "eink_tag")

#define NVS_DEVICE_ID_IS_PROJECT(id) (((uint32_t)(id) & 0xFFFFFF00U) == NVS_PROJECT_ID_CAT_PROJECT)

/** 硬件型号 ID（NVS 键 hid）：0x5353HHNN（HH=类别 0x02） */
#define NVS_HARDWARE_ID_CAT 0x53530200U
#define NVS_HARDWARE_ID_MAKE(index) (NVS_HARDWARE_ID_CAT | ((uint32_t)(index) & 0xFFU))
#define NVS_HARDWARE_ID_NRF54L15_DK    NVS_HARDWARE_ID_MAKE(0x10U)
#define NVS_HARDWARE_ID_NRF54L15_REV_A NVS_HARDWARE_ID_MAKE(0x11U)

#ifndef NVS_DEFAULT_HARDWARE_ID
#ifdef CONFIG_NRF54_NVS_DEFAULT_HARDWARE_ID
#define NVS_DEFAULT_HARDWARE_ID CONFIG_NRF54_NVS_DEFAULT_HARDWARE_ID
#else
#define NVS_DEFAULT_HARDWARE_ID NVS_HARDWARE_ID_NRF54L15_DK
#endif
#endif

#ifndef NVS_DEFAULT_DEVICE_ID
#ifdef CONFIG_NRF54_NVS_DEFAULT_DEVICE_ID
#define NVS_DEFAULT_DEVICE_ID CONFIG_NRF54_NVS_DEFAULT_DEVICE_ID
#else
#define NVS_DEFAULT_DEVICE_ID NVS_DEVICE_ID_DEFAULT
#endif
#endif

/* -------------------------------------------------------------------------- */
/* 容量与默认值                                                                */
/* -------------------------------------------------------------------------- */

#define NVS_MAC_SIZE 8
#define NVS_SN_SIZE 16
#define NVS_HW_VERSION_SIZE 16
#define NVS_APP_VERSION_SIZE 24
#define NVS_FACTORY_VERSION_SIZE 24

#define NVS_DEFAULT_MAC \
	{ 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 }
#define NVS_DEFAULT_SN "T90M0123456789"
#define NVS_HW_VERSION_STRING "1.0.0"
#define NVS_DEFAULT_DEVICE_TYPE 1

#define NVS_FLASH_START_ADDR FLASH_PART_NVS_START
#define NVS_FLASH_SIZE       FLASH_PART_NVS_SIZE
#define NVS_APP_NAMESPACE    "ty_app"

#define NVS_BUILD_DATE_SIZE 9

/** boot_mode（E1 厂测入口）；Boot 将来读此键 */
#define NVS_BOOT_MODE_APP     0U
#define NVS_BOOT_MODE_FACTORY 1U

/** 墨水屏面板尺寸（NVS 键 epanel）；上电选驱动 */
#define NVS_EPD_PANEL_2IN9 0U /**< GDEY029Z95 2.9" BWR 128×296 */
#define NVS_EPD_PANEL_4IN2 1U /**< WS 4.2" B V2 BWR 400×300 */

/* -------------------------------------------------------------------------- */
/* 生命周期                                                                    */
/* -------------------------------------------------------------------------- */

void nvs_init(void);
void nvs_factory_reset(void);

/* -------------------------------------------------------------------------- */
/* 设备标识                                                                    */
/* -------------------------------------------------------------------------- */

bool nvs_mac_set(const uint8_t *mac);
bool nvs_mac_get(uint8_t *mac);
bool nvs_sn_set(const char *sn);
bool nvs_sn_get(char *sn);
bool nvs_hw_version_get(char *version);
bool nvs_app_version_get(char *version);
bool nvs_factory_version_get(char *version);
void nvs_device_type_set(uint8_t type);
uint8_t nvs_device_type_get(void);

bool nvs_device_id_set(uint32_t id);
uint32_t nvs_device_id_get(void);
#define nvs_product_id_get nvs_device_id_get
#define nvs_product_id_set nvs_device_id_set

bool nvs_hardware_id_set(uint32_t id);
uint32_t nvs_hardware_id_get(void);

/**
 * 运行时 product_id：非 0 的编译期 NVS_DEFAULT_DEVICE_ID 优先；
 * factory（默认为 0）读 NVS did。
 */
uint32_t nvs_product_id_active(void);
#define nvs_device_id_for_init nvs_product_id_active

const char *nvs_device_id_project_name(uint32_t id);
#define nvs_product_id_name nvs_device_id_project_name

uint32_t nvs_firmware_build_date(void);
uint32_t nvs_build_date_get(void);
bool nvs_build_date_format(uint32_t ymd, char *out, size_t out_cap);

/* -------------------------------------------------------------------------- */
/* 启动模式（storage 标志 → 将来 MCUBoot 读）                                  */
/* -------------------------------------------------------------------------- */

bool nvs_boot_mode_set(uint8_t mode);
uint8_t nvs_boot_mode_get(void);

bool nvs_epd_panel_set(uint8_t panel);
uint8_t nvs_epd_panel_get(void);

/** 上电后打印 NVS 缓存摘要（MAC/SN/did/hid/版本等）。 */
void nvs_log_boot_dump(void);

#ifdef __cplusplus
}
#endif

#endif /* COMMON_NVS_H */
