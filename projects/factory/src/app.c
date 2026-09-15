/*
 * factory — project logic only (platform boot is in start/).
 * FTM 跳槽：board 配 BTN_PERMISSION_FTM；序列在 common button_ftm_seq_poll（start_poll）。
 */

#include "start.h"

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "cmd.h"
#include "device_profile.h"
#include "flash_partition.h"
#include "log.h"
#include "nvs.h"

/** PC-based slot guess (swap-move images are primary-linked; see plan note). */
static uint8_t app_probe_slot(void)
{
	uintptr_t here = (uintptr_t)app_probe_slot;

	if ((here >= (uintptr_t)PART_APP_B_START) && (here < (uintptr_t)PART_APP_B_END)) {
		return 1U;
	}
	return 0U;
}

/*
 * 厂测按 did 分支初始化。新产品：在 nvs.h 加 NVS_PROJECT_ID_*，这里加 if 分支。
 * 例：if (did == NVS_PROJECT_ID_EINK_TAG) { 初始化对应外设 }
 */
static void factory_hw_init_eink_tag(void)
{
	LOG_INFO("factory hw init: eink_tag (stub)");
}

static void factory_hw_init_demo(void)
{
	LOG_INFO("factory hw init: demo (stub)");
}

static void factory_hw_init_default(void)
{
	LOG_INFO("factory hw init: default");
}

static void factory_hw_init(void)
{
	uint32_t did = nvs_device_id_get();

	LOG_INFO("factory hw init did=0x%08x (%s)", did, nvs_device_id_project_name(did));

	if (did == NVS_PROJECT_ID_EINK_TAG) {
		factory_hw_init_eink_tag();
	} else if (did == NVS_PROJECT_ID_DEMO) {
		factory_hw_init_demo();
	} else {
		factory_hw_init_default();
	}
}

status_t app_init(void)
{
	uint8_t slot = app_probe_slot();
	uint8_t bmode = nvs_boot_mode_get();

	cmd_init(NULL, NULL);
	cmd_register_defaults();
	factory_hw_init();

	LOG_INFO("factory app_init ok hw=%s slot=%u bmode=%u",
		 device_profile_hardware_name(device_profile_hardware_id()),
		 (unsigned)slot, (unsigned)bmode);
	printk("factory printk ok (uart20 P1.09/P1.10) slot=%u bmode=%u\n",
	       (unsigned)slot, (unsigned)bmode);
	return STATUS_OK;
}

void app_run(void)
{
	int64_t next_hb_ms = k_uptime_get() + 10000;

	LOG_INFO("factory app_run (type help; FTM seq in common)");

	while (1) {
		start_poll();
		cmd_poll();

		if (k_uptime_get() >= next_hb_ms) {
			LOG_INFO("factory heartbeat");
			next_hb_ms += 10000;
		}

		k_msleep(20);
	}
}
