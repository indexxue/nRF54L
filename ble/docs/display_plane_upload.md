# BLE 墨水屏平面上传协议

Host 通过 GATT 分片上传 **MCU 帧缓冲**（非绘图指令），设备 CRC 校验后全刷。  
对应固件：`ble/include/ble_cmd.h`、`projects/eink_tag`。

## 前提

| 项 | 值 |
|----|-----|
| 设备名 | `eink_tag` |
| Service | `{FNV1a32(sn):08x}-0001-1000-8000-00805f9b34fb`（台台不同） |
| RX Write | 同前缀 `…-0002-…` |
| TX Notify | 同前缀 `…-0003-…` |
| 面板 | GDEY029Z95 128×296 BWR |
| 平面大小 | **4736** 字节（16 字节/行 × 296） |
| 帧格式 | `cmd_id u16 LE` + `payload_len u16 LE` + `payload[]`，payload ≤ 240 |

须对 TX **Enable Notify**，否则 ACK 发不出去。

## 命令

| ID | 宏 | 方向 | 作用 |
|----|-----|------|------|
| 1001 | `BLE_CMD_PING` | ↓ | 链路冒烟，不碰屏 |
| 2001 | `BLE_CMD_DISPLAY` | ↓ | 本地 demo 全刷（与上传无关） |
| **2002** | `BLE_CMD_DISP_BEGIN` | ↓ | 开会话：声明平面 / len / CRC，并清对应缓冲 |
| **2003** | `BLE_CMD_DISP_CHUNK` | ↓ | 按 offset 写平面分片 |
| **2004** | `BLE_CMD_DISP_REFRESH` | ↓ | CRC 通过后异步全刷 |
| 7001 | `BLE_CMD_ACK` | ↑ | 每包应答：`req_cmd_id u16` + `status i32` |
| 8001 | `BLE_CMD_HEARTBEAT` | ↑ | 心跳（约 20s） |

## 会话流程

```
IDLE ──BEGIN──► RECEIVING ──CHUNK×N──► RECEIVING ──REFRESH──► IDLE
                  ▲                         │
                  └──────── 再次 BEGIN ──────┘（覆盖旧会话）
```

1. **BEGIN**：校验分辨率；**始终清空** BW+RED 缓冲（BW→`0xFF`，RED→`0x00`）；按 `planes` 记期望 len/CRC；进入 `RECEIVING`  
   （未在 `planes` 中声明的平面保持清屏态，刷新时仍会写出，避免旧红/旧黑叠影）
2. **CHUNK**：仅 `RECEIVING`；写入 `plane` 在 `offset` 起的 `data[]`；每包 ACK
3. **REFRESH**：对已声明平面做整平面 CRC32；失败则回 `IDLE` 且 ACK≠0；成功则**锁定缓冲**、入队异步全刷、回 `IDLE`，ACK=0
4. 刷屏进行中：`BEGIN` / `CHUNK` / 再次刷屏 → `STATUS_INVALID_STATE`（-6）

设备只维护 **当前一屏** live buffer，无 page 槽位。

## Payload

### 2002 `DISP_BEGIN`（固定 **18** 字节）

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | `width` | u16 LE | 须为 **128** |
| 2 | `height` | u16 LE | 须为 **296** |
| 4 | `planes` | u8 | bit0=BW，bit1=RED；至少一个 |
| 5 | `flags` | u8 | 预留，须为 **0** |
| 6 | `bw_len` | u16 LE | 含 BW 时为 **4736**，否则 0 |
| 8 | `red_len` | u16 LE | 含 RED 时为 **4736**，否则 0 |
| 10 | `bw_crc` | u32 LE | 整平面 CRC32；未传 BW 填 0 |
| 14 | `red_crc` | u32 LE | 同上 |

### 2003 `DISP_CHUNK`（5 + N 字节，N≤235）

| 偏移 | 字段 | 类型 | 说明 |
|------|------|------|------|
| 0 | `plane` | u8 | `0`=BW，`1`=RED |
| 1 | `offset` | u16 LE | 平面内字节偏移 |
| 3 | `data_len` | u16 LE | 本包数据长度 N |
| 5 | `data` | N 字节 | 原始平面数据 |

约束：`payload_len == 5 + data_len`；`offset + data_len ≤ 4736`；`plane` 须在 BEGIN 的 `planes` 内。

### 2004 `DISP_REFRESH`

空 payload（`payload_len = 0`）。

## 平面字节约定（对齐 `cbb/epd` 帧缓冲）

上传的是 **已映射到 panel RAM 顺序** 的 MCU 缓冲，刷屏时原样写入 `0x24`（BW）/ `0x26`（RED）。

| 项 | 约定 |
|----|------|
| 布局 | 行优先；每行 16 字节 |
| 字节内位序 | **MSB = 更靠左的像素**（`bit = 7 - (x % 8)`） |
| Y 方向 | 本板 `flip_y=true`：逻辑坐标原点在左上时，**RAM 行 0 = 逻辑底行**。打包公式：`ram_y = 295 - logical_y`，`idx = ram_y * 16 + x/8` |
| BW 极性 | **`1` = 白，`0` = 黑**（SSD1680；本板 `bw_invert=false`） |
| RED 极性 | **`1` = 红，`0` = 非红**；红像素时 BW 对应位应为白（`1`） |

> 纠偏：口头草案曾写「BW 1=黑」；以驱动 `set_bw_bit(white→置 1)` 为准，文档与固件按 **1=白**。

## CRC32

- 算法：CRC-32/ISO-HDLC（多项式 `0xEDB88320`，初值 `0xFFFFFFFF`，结果取反）
- 与 Python `zlib.crc32(data) & 0xffffffff`、仓库 `common/crc32.h` 的 `crc32_compute()` 一致
- 对 **整平面** `bw_len` / `red_len` 字节计算（BEGIN 已清屏，未覆盖区域参与 CRC）

## ACK status（常用）

| 值 | 宏 | 典型原因 |
|----|-----|----------|
| 0 | `STATUS_OK` | 成功 |
| -1 | `STATUS_FAIL` | CRC 不符 / 队列满 |
| -2 | `STATUS_INVALID_ARG` | 长度/平面/分辨率错误 |
| -5 | `STATUS_NOT_SUPPORTED` | `flags≠0` 或未知命令 |
| -6 | `STATUS_INVALID_STATE` | 未 BEGIN、EPD 未就绪、刷屏中 |

## Host 伪代码

```python
import struct, zlib

PLANE = 4736

def crc32(b: bytes) -> int:
    return zlib.crc32(b) & 0xFFFFFFFF

def pack_begin(bw: bytes | None, red: bytes | None) -> bytes:
    planes = (0x01 if bw is not None else 0) | (0x02 if red is not None else 0)
    return struct.pack(
        "<HHBBHHII",
        128, 296, planes, 0,
        PLANE if bw is not None else 0,
        PLANE if red is not None else 0,
        crc32(bw) if bw is not None else 0,
        crc32(red) if red is not None else 0,
    )

def iter_chunks(plane_id: int, data: bytes, max_n: int = 235):
    off = 0
    while off < len(data):
        n = min(max_n, len(data) - off)
        yield struct.pack("<BHH", plane_id, off, n) + data[off : off + n]
        off += n

# send(2002, pack_begin(bw, red)); wait_ack
# for p in iter_chunks(0, bw): send(2003, p); wait_ack
# for p in iter_chunks(1, red): send(2003, p); wait_ack   # optional
# send(2004, b""); wait_ack
```

## 与 demo 的关系

- `2001 DISPLAY`：仍触发板级 BWR demo，不读 payload
- Host 上传请只用 `2002→2003→2004`
- 刷屏均为 EPD 工作线程异步执行；ACK 在入队成功时返回，**不等于**面板已显示完成
- 设备侧建议 `BT_L2CAP_TX_MTU≥247`（见 `projects/eink_tag/prj.conf`）。默认 MTU=23 时，大包 Write Request 可能触发 Prepare Write → **Request Not Supported**
