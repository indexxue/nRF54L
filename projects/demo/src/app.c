/*
 * demo — project logic only (platform boot is in start/).
 * With MCUBoot: this image runs from slot0 (image-0); factory lives in slot1.
 * FTM 跳槽：board 配 BTN_PERMISSION_FTM；序列在 common button_ftm_seq_poll（start_poll）。
 */

#include "start.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "board.h"
#include "device_profile.h"
#include "log.h"
#include "nvs.h"

status_t app_init(void)
{
	uint8_t bmode = nvs_boot_mode_get();

	LOG_INFO("demo app_init ok hw=%s bmode=%u (expect slot0/image-0)",
		 device_profile_hardware_name(device_profile_hardware_id()),
		 (unsigned)bmode);
	printk("demo printk ok (uart20) bmode=%u image-0\n", (unsigned)bmode);
	/* nordic-flpr: cpuapp copies FLPR image to SRAM and starts RISC-V VPR */
	LOG_INFO("FLPR (RISC-V) co-processor enabled — see remote/ + uart30");
	return STATUS_OK;
}

void app_run(void)
{
	uint32_t tick = 0U;

	LOG_INFO("demo run (start_poll 20ms, heartbeat 1s; FTM seq in common)");

	while (1) {
		start_poll();
		if ((tick % 50U) == 0U) {
			LOG_INFO("demo heartbeat tick=%u board=%s core=cpuapp",
				 tick / 50U, board_config_get()->name);
		}
		tick++;
		k_sleep(K_MSEC(20));
	}
}
