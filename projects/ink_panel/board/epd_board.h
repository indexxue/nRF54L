/**
 * @file epd_board.h
 * @brief ink_panel EPD：bitbang/SPIM20 + nPM1300；平面上传 API 对齐 eink_tag
 */

#ifndef INK_PANEL_EPD_BOARD_H
#define INK_PANEL_EPD_BOARD_H

#include "type.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPD_BOARD_PLANE_BW  0U
#define EPD_BOARD_PLANE_RED 1U

#define EPD_BOARD_PLANES_BW  (1U << 0)
#define EPD_BOARD_PLANES_RED (1U << 1)

/** 产品仅 4.2"（历史 2.9 常量保留兼容，勿再选） */
#define EPD_BOARD_SIZE_2IN9 0U
#define EPD_BOARD_SIZE_4IN2 1U

status_t epd_board_bringup(void);

/** 本地 BWR demo（阻塞；仅 worker / DISPLAY 用） */
status_t epd_board_test_run(void);

/** 启动 EPD worker：bringup → 显示 home/0 → 处理 job 队列 */
void epd_board_start(void);

/** 入队 demo（DISPLAY 2001） */
status_t epd_board_demo_request(void);

/** 入队 host 缓冲刷新 */
status_t epd_board_refresh_request(void);

/**
 * 将当前平面写入 Flash 页槽（K_NO_WAIT）。
 * @param show true：刷屏后再落槽；false：只写槽不改当前显
 */
status_t epd_board_store_refresh_request(uint8_t mode, uint8_t page, bool show);

/** 从 Flash 页槽加载到平面缓冲并全刷（K_NO_WAIT）。空槽→白屏。 */
status_t epd_board_show_slot_request(uint8_t mode, uint8_t page);

/**
 * 将当前 MCU 帧缓冲全刷到屏（Host 上传路径）。
 * 若未预先 lock，本函数会自行加锁。
 */
status_t epd_board_refresh(void);

status_t epd_board_lock_planes(void);
void epd_board_unlock_planes(void);

bool epd_board_is_ready(void);
/** 平面锁占用（推显存中）；BEGIN/CHUNK 看这个 */
bool epd_board_planes_busy(void);
/** 面板物理刷新中；下一张 REFRESH 仍可入队 */
bool epd_board_panel_busy(void);
/** planes || panel（心跳 / 总览） */
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
status_t epd_board_write_plane(uint8_t plane, uint16_t offset, const uint8_t *data,
			       uint16_t len);

/** 只读平面指针（供 CRC）；无效 plane 返回 NULL。 */
const uint8_t *epd_board_plane_data(uint8_t plane);

/**
 * 软休眠：panel sleep（若可）→ SPI 脚 Hi-Z → 关 LDSW2。
 * 忙时返回 STATUS_INVALID_STATE。
 */
status_t epd_board_rails_suspend(void);

/** 恢复脚驱动 + 开轨；下次刷屏再 epd_init（s_panel_asleep）。 */
status_t epd_board_rails_resume(void);

bool epd_board_rails_suspended(void);

#ifdef __cplusplus
}
#endif

#endif /* INK_PANEL_EPD_BOARD_H */
