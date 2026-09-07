/**
 * @file ble_cmd.h
 * @brief 标定 CMD：cmd_id + payload；ingress → CMD 线程 → EVT_ID_BLE_CMD。
 *
 * 包格式（小端）：uint16_t cmd_id | uint16_t payload_len | payload[]
 *
 * cmd_id 仅用四位数 1000–9999（0 / 65535 禁用）：
 *
 *   下行 Host→设备（Write）
 *     1000–1999  链路
 *     2000–2999  显示
 *     3000–3999  控制
 *     4000–4999  配置
 *     5000–5999  厂测 / OTA
 *     9000–9999  诊断 / 私有
 *
 *   上行 设备→Host（Notify；统一 ACK）
 *     7000–7999  ACK
 *     8000–8999  上报 / 异常 / 进度
 */

#ifndef NRF54_BLE_CMD_H
#define NRF54_BLE_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "type.h"

#define BLE_CMD_HDR_SIZE     4U
#define BLE_CMD_PAYLOAD_MAX  240U
#define BLE_CMD_FRAME_MAX    (BLE_CMD_HDR_SIZE + BLE_CMD_PAYLOAD_MAX)

/* ---- 四位数区段（含端点）---- */

#define BLE_CMD_RANGE_LINK_MIN     1000U
#define BLE_CMD_RANGE_LINK_MAX     1999U

#define BLE_CMD_RANGE_DISPLAY_MIN  2000U
#define BLE_CMD_RANGE_DISPLAY_MAX  2999U

#define BLE_CMD_RANGE_CONTROL_MIN  3000U
#define BLE_CMD_RANGE_CONTROL_MAX  3999U

#define BLE_CMD_RANGE_CONFIG_MIN   4000U
#define BLE_CMD_RANGE_CONFIG_MAX   4999U

#define BLE_CMD_RANGE_FACTORY_MIN  5000U
#define BLE_CMD_RANGE_FACTORY_MAX  5999U

#define BLE_CMD_RANGE_ACK_MIN      7000U
#define BLE_CMD_RANGE_ACK_MAX      7999U

#define BLE_CMD_RANGE_REPORT_MIN   8000U
#define BLE_CMD_RANGE_REPORT_MAX   8999U

#define BLE_CMD_RANGE_DIAG_MIN     9000U
#define BLE_CMD_RANGE_DIAG_MAX     9999U

#define BLE_CMD_IN_RANGE(id, lo, hi) (((uint16_t)(id) >= (lo)) && ((uint16_t)(id) <= (hi)))

#define BLE_CMD_IS_LINK(id)     BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_LINK_MIN, BLE_CMD_RANGE_LINK_MAX)
#define BLE_CMD_IS_DISPLAY(id)  BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_DISPLAY_MIN, BLE_CMD_RANGE_DISPLAY_MAX)
#define BLE_CMD_IS_CONTROL(id)  BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_CONTROL_MIN, BLE_CMD_RANGE_CONTROL_MAX)
#define BLE_CMD_IS_CONFIG(id)   BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_CONFIG_MIN, BLE_CMD_RANGE_CONFIG_MAX)
#define BLE_CMD_IS_FACTORY(id)  BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_FACTORY_MIN, BLE_CMD_RANGE_FACTORY_MAX)
#define BLE_CMD_IS_ACK(id)      BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_ACK_MIN, BLE_CMD_RANGE_ACK_MAX)
#define BLE_CMD_IS_REPORT(id)   BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_REPORT_MIN, BLE_CMD_RANGE_REPORT_MAX)
#define BLE_CMD_IS_DIAG(id)     BLE_CMD_IN_RANGE((id), BLE_CMD_RANGE_DIAG_MIN, BLE_CMD_RANGE_DIAG_MAX)

#define BLE_CMD_IS_DOWNLINK(id)                                                                  \
	(BLE_CMD_IS_LINK(id) || BLE_CMD_IS_DISPLAY(id) || BLE_CMD_IS_CONTROL(id) ||               \
	 BLE_CMD_IS_CONFIG(id) || BLE_CMD_IS_FACTORY(id) || BLE_CMD_IS_DIAG(id))

#define BLE_CMD_IS_UPLINK(id) (BLE_CMD_IS_ACK(id) || BLE_CMD_IS_REPORT(id))

/* ---- 已分配 ---- */

#define BLE_CMD_PING         1001U /**< 下行测试包：连通 / 冒烟 */
#define BLE_CMD_DISPLAY      2001U /**< 下行：触发本地 demo 全刷（与平面上传无关） */
#define BLE_CMD_DISP_BEGIN   2002U /**< 下行：开会话，声明平面 / 长度 / CRC */
#define BLE_CMD_DISP_CHUNK   2003U /**< 下行：按 offset 写入平面分片 */
#define BLE_CMD_DISP_REFRESH 2004U /**< 下行：CRC 通过后全刷当前缓冲 */
#define BLE_CMD_ACK          7001U /**< 上行统一应答 */
#define BLE_CMD_HEARTBEAT    8001U /**< 上行心跳上报 */

/** ACK payload：req_cmd_id(u16 LE) + status(i32 LE) */
#define BLE_CMD_ACK_PAYLOAD_SIZE 6U

/** HEARTBEAT payload：tick(u32 LE) + flags(u8: bit0=adv bit1=connected) */
#define BLE_CMD_HEARTBEAT_PAYLOAD_SIZE 5U

/** DISP_BEGIN payload 固定 18 B（见 ble/docs/display_plane_upload.md） */
#define BLE_CMD_DISP_BEGIN_PAYLOAD_SIZE 18U

/** DISP_CHUNK 头：plane(u8) + offset(u16) + data_len(u16) */
#define BLE_CMD_DISP_CHUNK_HDR_SIZE 5U
#define BLE_CMD_DISP_CHUNK_DATA_MAX (BLE_CMD_PAYLOAD_MAX - BLE_CMD_DISP_CHUNK_HDR_SIZE)

#define BLE_CMD_DISP_PLANE_BW  0U
#define BLE_CMD_DISP_PLANE_RED 1U

#define BLE_CMD_DISP_PLANES_BW  (1U << 0)
#define BLE_CMD_DISP_PLANES_RED (1U << 1)

typedef struct {
	uint16_t cmd_id;
	uint16_t payload_len;
	uint8_t payload[BLE_CMD_PAYLOAD_MAX];
} ble_cmd_msg_t;

status_t ble_cmd_thread_start(void);
status_t ble_cmd_ingress(const uint8_t *data, size_t len);
bool ble_cmd_fetch(ble_cmd_msg_t *out);

/** 组帧并通过 GATT Notify 上发（未连接或未开 CCC 返回 STATUS_INVALID_STATE）。 */
status_t ble_cmd_send(uint16_t cmd_id, const void *payload, uint16_t payload_len);

/** 统一 ACK：req_cmd_id + status。 */
status_t ble_cmd_send_ack(uint16_t req_cmd_id, status_t status);

/** 上行心跳：tick + flags（bit0=adv bit1=connected）。 */
status_t ble_cmd_send_heartbeat(uint32_t tick, uint8_t flags);

#ifdef __cplusplus
}
#endif

#endif /* NRF54_BLE_CMD_H */
