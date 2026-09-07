# ble/

与 [`common/`](../common/) 同级的 **BLE 协议层**：Host/GAP/GATT + 标定 CMD 包 + 独立 CMD 线程。

工程通过 Zephyr 模块 [`modules/nrf54_ble`](../modules/nrf54_ble) 接入；`app` 只 include `ble.h` / `ble_cmd.h`。

## 启动

```
main → start_boot → app_init → ble_init()
  → ble_cmd_thread_start → ble_stack → ble_gatt → ble_gap adv
```

## GATT

| 特征 | UUID 后缀 | 属性 |
|------|-----------|------|
| RX | `…0002…` | Write / Write Without Response → `ble_cmd_ingress` |
| TX | `…0003…` | Notify ← `ble_cmd_send` / ACK / HEARTBEAT |

Host 须对 TX **Enable Notify**，否则上发返回 `STATUS_INVALID_STATE`。

## 包格式（小端）

| 偏移 | 字段 |
|------|------|
| 0 | `cmd_id` u16（四位数 1000–9999） |
| 2 | `payload_len` u16 |
| 4 | `payload[]` |

## cmd_id 分区

| 区段 | 方向 | 类别 |
|------|------|------|
| 1000–1999 | 下行 | 链路 |
| 2000–2999 | 下行 | 显示 |
| 3000–3999 | 下行 | 控制 |
| 4000–4999 | 下行 | 配置 |
| 5000–5999 | 下行 | 厂测 / OTA |
| 7000–7999 | 上行 | 统一 ACK |
| 8000–8999 | 上行 | 上报 |
| 9000–9999 | 下行 | 诊断 / 私有 |

## 已分配

| ID | 宏 | 方向 | payload |
|----|-----|------|---------|
| 1001 | `BLE_CMD_PING` | 下行测试 | 可空 |
| 2001 | `BLE_CMD_DISPLAY` | 下行 | 本地 demo 全刷（可空） |
| 2002 | `BLE_CMD_DISP_BEGIN` | 下行 | 18 B：分辨率 / planes / len / CRC |
| 2003 | `BLE_CMD_DISP_CHUNK` | 下行 | plane + offset + data |
| 2004 | `BLE_CMD_DISP_REFRESH` | 下行 | 空；CRC 后异步全刷 |
| 7001 | `BLE_CMD_ACK` | 上行 | `req_cmd_id u16` + `status i32` |
| 8001 | `BLE_CMD_HEARTBEAT` | 上行 | `tick u32` + `flags u8`（bit0=adv bit1=conn） |

帧缓冲上传细节见 **[docs/display_plane_upload.md](docs/display_plane_upload.md)**。

API：`ble_cmd_send` / `ble_cmd_send_ack` / `ble_cmd_send_heartbeat`。

## 手机侧冒烟

1. 连接 `eink_tag`，对 TX 开 Notify  
2. 向 RX Write：**4 字节** `E9 03 00 00`（PING 1001 LE + plen=0）→ 应收到 ACK Notify  
3. 约每秒应收到 HEARTBEAT Notify  
