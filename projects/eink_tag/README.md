# eink_tag

通过 **BLE** 刷墨水屏标签；读卡走外挂 **PN5180**。  
新板整脚见 [`doc/board_io_rev_b.md`](doc/board_io_rev_b.md)（既有外设重排 + PN5180）。  
**当前冒烟**：外部 **HFXO** + UART/LED/按键（rev_b）+ **BLE** + **2.9"/4.2" BWR EPD**。  
注意：`CONFIG_START_REQUEST_HFXO=y`；晶振未起振会卡在 `nvs dump` 后无灯无日志。

平台起机：[`start/`](../../start/)。本工程根目录 `app.c` 只做 BLE + EPD。

## 当前接线（rev_b）

整表与硬件说明见 [`doc/board_io_rev_b.md`](doc/board_io_rev_b.md)。**勿烧到现网冒烟板（rev_a）**。

| 功能 | 引脚 |
|------|------|
| UART TX / RX | **P1.04** / **P1.05** |
| LED_G / LED_B | **P2.08** / **P2.09**（ACTIVE_LOW） |
| BTN1 / BTN2 | **P0.00** / **P0.01**（ACTIVE_LOW） |
| EPD SCK / MOSI / MISO / CS | **P2.01** / **P2.02** / **P2.04** / **P2.05**（SPI00） |
| EPD DC / RST / BUSY | **P2.00** / **P2.03** / **P2.07** |
| PN5180 SCK/MOSI/MISO/NSS | **P1.11** / **P1.12** / **P1.13** / **P1.14**（SPI22；业务后接） |
| PN5180 IRQ / BUSY / RST | **P0.02** / **P1.08** / **P1.07**（DT 已登记） |
| ADC AIN2 | **P1.06**（预留） |
| BLE 2.4G | 片上 RADIO（设备名 `eink_tag`） |
| HFCLK | 外部 **32 MHz HFXO** |
| LFCLK | 内部 RC |

`board` mask：`UART | LED | BUTTON | SPI | BLE`（IRQ/ADC 业务阶段再挂）

## 面板尺寸切换（BTN1 / CONFIRM）

| 操作 | 效果 | 灯效 |
|------|------|------|
| 上电～EPD ready | 初始化中 | **绿/蓝交替闪** |
| EPD ready 后 | 当前驱动指示 | **2.9" 绿灯常亮** / **4.2" 蓝灯常亮** |
| **双击** BTN1 | 切 **4.2"**，写 NVS，冷重启 | 先蓝常亮再重启 |
| **长按 10s** BTN1 | 切回 **2.9"**，写 NVS，冷重启 | 先绿常亮再重启 |

BLE 协议在仓库 [`ble/`](../../ble/)（`modules/nrf54_ble`）。  
- 上电：EPD **只 init，不跑 demo**（屏保持上一次画面）  
- 下行测试：`1001` PING → 仅 ACK，**不刷屏**；字节 `E9 03 00 00`  
- 下行 demo：`2001` DISPLAY → **异步 demo 全刷**  
- 下行上传：`2002 BEGIN` → `2003 CHUNK`×N → `2004 REFRESH`（帧缓冲，见 [`ble/docs/display_plane_upload.md`](../../ble/docs/display_plane_upload.md)）  
- 上行心跳：`8001` HEARTBEAT（约 **20s**，需 TX Notify）  

等串口出现 `epd: ready` 后再发 DISPLAY / 上传；刷屏期间灯效/BLE 不停。

## 低功耗（按键唤醒 + 广播窗口）

市面常见 BLE 标签机制：

```text
[睡眠: 停广播 / EPD sleep / 灭灯]
        ↑ 可发现窗口超时(30s)         ↓ 带 WAKE 权限按键
[可发现: 快广播 30s] ←────────────────┘
        ↓ 手机连接
[已连接: 保持工作]
        ↓ 用户断连
[可发现: 再广播 30s] → 超时 → [睡眠]
```

| 项 | 行为 |
|----|------|
| 睡眠 | **停广播** + EPD sleep + 灭灯 + main `event_wait` |
| 唤醒 | **`BTN_PERMISSION_WAKE` 按键** → 开快广播 30s |
| 断连 | 自动再广播，再等 **30s** 可发现窗口后才进睡 |
| 窗口内按键 | 刷新 30s 计时 |

串口关键字：`app: SLEEP enter` / `app: SLEEP exit → advertising`。

电流验收：睡眠（无广播）应明显低于可发现窗口；测电流时可再关 UART。

**不要用** nRF Connect 的 SMP / DFU / UART 菜单（会报 `SMP service not found`）。用自定义 128-bit 服务：

| 角色 | UUID |
|------|------|
| Service | `{FNV1a32(sn):08x}-0001-1000-8000-00805f9b34fb` |
| RX（手机→设备 Write） | `{…}-0002-1000-8000-00805f9b34fb` |
| TX（设备→手机 Notify） | `{…}-0003-1000-8000-00805f9b34fb` |

前 32 bit = **FNV-1a(当前 NVS `sn`)**（与 `ble_gatt_prepare` 一致）。  
首启无 `sn` 时自动生成 `T90M` + 8 hex 并落盘；串口 `nvs dump` / `ble_gatt: sn=… uuid=…` 可核对。  
厂测用 `sn` / `sn <value>` 改号（厂测不开 BLE，无需重启；量产镜像 UUID 在 `ble_init` 时按当时 sn 固化到本次运行）。

nRF Connect 步骤：

1. 连接 **`eink_tag`** → 点 **Unknown Service**（UUID 前缀因台而异，后缀 `…0001…`）  
2. 对 **TX**（`…0003…`）点三下箭头 / Enable **Notify**  
3. 对 **RX**（`…0002…`）Write，BYTE ARRAY：  
   - 链路测试 PING：`E9030000`（或 `E9 03 00 00`）  
   - **刷墨水屏 DISPLAY**：`D1070000`（或 `D1 07 00 00`）＝ cmd **2001**  

```powershell
.\nrf.cmd -Project eink_tag build
.\nrf.cmd -Project eink_tag flash
```

扫描 **`eink_tag`**；串口应见 `injected BLE_CMD_PING`；连上并开 Notify 后见 ACK / HEARTBEAT。
