/*
 * demo remote — runs on nRF54L15 FLPR (RISC-V VPR), not on Cortex-M33.
 *
 * Boot path: cpuapp loads this image from RRAM @ 0x165000 into SRAM and
 * starts the FLPR core (nordic-flpr snippet / cpuflpr_vpr).
 *
 * Console: uart30 (DK / SoC default for cpuflpr). App console stays uart20.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	uint32_t tick = 0U;

	printk("FLPR (RISC-V) up on %s\n", CONFIG_BOARD_TARGET);

	while (1) {
		printk("FLPR heartbeat tick=%u\n", tick);
		tick++;
		k_sleep(K_MSEC(2000));
	}

	return 0;
}
