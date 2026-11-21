/*
 * ink_panel — EVT 冒烟：UART + LED + nPM + EPD
 * EPD：对齐 eink_tag（erase + BWR demo）；BTN1 单击入队重刷，不断 RTT。
 */

#include "start.h"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/util.h>

#include "app_event.h"
#include "board.h"
#include "button.h"
#include "npm1300_board.h"
#include "nvs.h"

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
#include "epd_board.h"
#endif

status_t app_init(void)
{
	printk("ink_panel boot (uart+led+npm+epd eink_tag-aligned)\n");
	button_long_press_ms_set_id(BTN_ID_CONFIRM, 5000U);

	if (npm1300_board_bringup() != 0) {
		printk("npm1300 bringup fail (EPD may lack power)\n");
	}

#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
	epd_board_smoke_start();
#else
	printk("epd: disabled\n");
#endif

	printk("ink_panel ok board=%s\n", board_config_get()->name);
	return STATUS_OK;
}

static void app_handle_button(void)
{
	btn_id_e id;
	btn_event_e ev;

	if (!app_event_take(APP_EVT_BUTTON)) {
		return;
	}

	button_last_event_get(&id, &ev);
	button_last_event_clear();

	if ((id == BTN_ID_CONFIRM) && (ev == BTN_EVENT_SINGLE_CLICK)) {
#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
		status_t st = epd_board_demo_request();

		if (st == STATUS_OK) {
			printk("app: BTN1 click -> queue EPD erase+demo\n");
		} else {
			printk("app: BTN1 click rejected st=%d (EPD busy/not ready)\n", (int)st);
		}
#else
		printk("app: BTN1 short (EPD off)\n");
#endif
		return;
	}

	if ((id == BTN_ID_CONFIRM) && (ev == BTN_EVENT_LONG_PRESS)) {
		printk("app: BTN1 long -> factory reboot\n");
		if (!nvs_boot_mode_set(NVS_BOOT_MODE_FACTORY)) {
			printk("app: factory boot_mode set fail\n");
			return;
		}
		k_msleep(50);
		sys_reboot(SYS_REBOOT_COLD);
	}
}

void app_run(void)
{
	uint32_t tick = 0U;

	printk("ink_panel run (LED + EPD eink_tag path; BTN1=re-demo)\n");

	while (1) {
		start_poll();
		app_handle_button();
		if ((tick % 50U) == 0U) {
			printk("ink_panel heartbeat tick=%u epd_ready=%d epd_busy=%d\n",
			       tick / 50U,
#if IS_ENABLED(CONFIG_INK_PANEL_EPD)
			       epd_board_is_ready() ? 1 : 0, epd_board_is_busy() ? 1 : 0
#else
			       0, 0
#endif
			);
		}
		tick++;
		k_msleep(20);
	}
}
