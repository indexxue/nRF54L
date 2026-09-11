/**
 * @file epd_board.h
 * @brief 工程侧墨水屏胶水：board SPI + GPIO → cbb/epd（2.9" / 4.2" BWR 可切换）。
 */

#ifndef EINK_TAG_EPD_BOARD_H
#define EINK_TAG_EPD_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

#define EPD_BOARD_PLANE_BW  0U
#define EPD_BOARD_PLANE_RED 1U

#define EPD_BOARD_PLANES_BW  (1U << 0)
#define EPD_BOARD_PLANES_RED (1U << 1)

/** 与 NVS_EPD_PANEL_* 同值 */
#define EPD_BOARD_SIZE_2IN9 0U
#define EPD_BOARD_SIZE_4IN2 1U

/**
 * SPI00 自检：device_ready + 寄存器 + MOSI↔MISO 回环。
 * 须在 epd_board_init() 之前调用；失败则不要测屏。
 */
status_t epd_board_spi_selftest(void);

/**
 * 配置 SPI/CS/DC/RST/BUSY，按 NVS epanel 选 GDEY029Z95 或 WS 4.2B V2，
 * 硬件 init（调用线程上可能阻塞数秒）。
 */
status_t epd_board_init(void);

/**
 * 黑白红 demo 全刷。
 * 阻塞**调用线程**（可能数十秒）；勿在 main 调用，以免卡住 start_poll 灯效。
 */
status_t epd_board_test_run(void);

/**
 * 将当前 MCU 帧缓冲全刷到屏（Host 上传路径）。
 * 若未预先 epd_board_lock_planes()，本函数会自行加锁。
 */
status_t epd_board_refresh(void);

/**
 * 刷屏前锁定平面（拒绝 write/clear/BEGIN）。
 * 与 queue 配对：入队失败须 unlock。
 */
status_t epd_board_lock_planes(void);
void epd_board_unlock_planes(void);

/**
 * 面板进 deep sleep（保持 MCU 帧缓冲）。忙时拒绝。
 * 下次刷屏前须 epd_board_wake()（refresh/test_run 内会自动 wake）。
 */
status_t epd_board_sleep(void);
status_t epd_board_wake(void);
bool epd_board_is_asleep(void);

bool epd_board_is_ready(void);
bool epd_board_is_busy(void);

uint8_t epd_board_size(void);
const char *epd_board_panel_name(void);

uint16_t epd_board_width(void);
uint16_t epd_board_height(void);
uint32_t epd_board_plane_size(void);

/**
 * 清平面：BW→全白(0xFF)，RED→全 0。
 * @param planes_mask bit0=BW bit1=RED
 */
status_t epd_board_clear_planes(uint8_t planes_mask);

/** 按偏移写入平面；越界返回 STATUS_INVALID_ARG。 */
status_t epd_board_write_plane(uint8_t plane, uint16_t offset, const uint8_t *data, uint16_t len);

/** 只读平面指针（供 CRC）；无效 plane 返回 NULL。 */
const uint8_t *epd_board_plane_data(uint8_t plane);

#ifdef __cplusplus
}
#endif

#endif /* EINK_TAG_EPD_BOARD_H */
