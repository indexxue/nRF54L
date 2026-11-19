# ink_panel

EVT 自有板 bring-up。无线链路为 **BLE Peripheral**（仓库 [`ble/`](../../ble/)）。

规格（历史 Zigbee 轮）：[`docs/superpowers/specs/2026-11-12-ink-panel-design.md`](../../docs/superpowers/specs/2026-11-12-ink-panel-design.md)  
本轮：Zigbee → BLE 档1（可连 + PING；EPD 上传后轮）。

| 层 | 路径 |
|----|------|
| 产品（按键/灯效/日志/CMD） | `src/app.c` |
| BLE 协议（仓库共享） | `ble/` via `modules/nrf54_ble` |

## 板级（EVT 网表）

| 功能 | 网名 | 引脚 |
|------|------|------|
| UART TX | `UART_TX` | **P2.02**（手册专用 UARTE TX；**不是**原理图误标的 P2.01） |
| UART RX | `UART_RX` | **P2.00**（UARTE RX；外接 TTL TX；只看日志可不接） |
| LED_STATUS | `LED_STATUS_N` | **P1.13** ACTIVE_LOW |
| BTN1 | `BTN_USER_N` | **P0.00** |
| BTN2 | — | **未接**（勿占用 `TOUCH_ALERT` P0.01） |
| BLE 2.4G | — | 片上 RADIO（设备名 `ink_panel`） |

## 按键 / LED / 低功耗

| 操作 | 行为 |
|------|------|
| BTN1 短按 | 打开/刷新 **30s** 可发现广播窗 + PAIRING 灯效 |
| BTN1 长按 ≈5s | `boot_mode=factory` + 冷复位（须 slot1 已烧 factory） |
| 广播窗超时且未连接 | 停广播进入浅睡；BTN1（WAKE）再开窗 |
| 已连接 | SUCCESS 灯效；断连后自动再广播并重新计时 30s |

## BLE 冒烟（nRF Connect）

自定义 128-bit 服务（前 32 bit = FNV-1a(NVS sn)），见 [`ble/README.md`](../../ble/README.md)。

| 步骤 | 期望 |
|------|------|
| 扫描连接 `ink_panel`，对 TX **Enable Notify** | 串口 `ble: connected` |
| RX Write：`E9 03 00 00`（PING 1001） | Notify ACK；串口 `app: PING ok` |
| 保持连接约 20s | HEARTBEAT Notify；串口 `app: HEARTBEAT` |
| 显示类 CMD（2001–2004） | ACK `STATUS_NOT_SUPPORTED`（档1 未开 EPD 上传） |

**不要用** nRF Connect 的 SMP / DFU / UART 菜单测业务。

## 构建 / 烧录

```powershell
.\nrf.cmd -Project ink_panel fullclean
.\nrf.cmd -Project ink_panel build-all
.\nrf.cmd -Project ink_panel flash

# 日常只改 APP：
.\nrf.cmd -Project ink_panel build
.\nrf.cmd -Project ink_panel flash
```

期望串口：`ink_panel boot` / `ink_panel ok` / `ble: advertising`（BTN1 或上电窗内）。

墨水屏：`prj.conf` 里 `CONFIG_INK_PANEL_EPD=n`（暂关）。要开屏改 `y` 后重编（画面上传协议仍后轮）。

NFC（片内 NFCT T2T）：`CONFIG_INK_PANEL_NFC=y`。期望串口 `nfc: bringup ok`；手机靠近见 `nfc: field on` / `data read` / `field off`；读到 Text `ink_panel NFC ok`。关改 `n`。
