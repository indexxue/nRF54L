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
| BTN1 **单击** | 入队 **EPD 擦白+BWR demo**（对齐 eink_tag；不复位，**RTT 不断**） |
| BTN1 长按 ≈5s | `boot_mode=factory` + 冷复位（须 slot1 已烧 factory） |

冒烟阶段以 EPD/RTT 为主；勿用复位键看日志（会断 J-Link RTT）。刷屏在 EPD 线程，main 只入队。

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

期望 RTT 日志：`ink_panel boot` / `ink_panel ok` / `epd: worker start` / `epd: boot demo done` 等。

## 日志（J-Link RTT）

本工程 **默认不用 UART 控制台**，`printk` 走 **SEGGER RTT**（见 `prj.conf`）：

```conf
CONFIG_USE_SEGGER_RTT=y
CONFIG_RTT_CONSOLE=y
CONFIG_UART_CONSOLE=n
```

须 **J-Link 接好 SWD 且目标在跑**（可先 `flash` 再开 RTT；Viewer 与烧录不要抢同一把锁太久）。

### 方式 A：仓库脚本（推荐）

仓库已备启动脚本（依赖同目录下的 J-Link 工具包）：

```powershell
# 图形界面（看 printk / demo 日志）
.\tmp\rtt\open_rtt_viewer.cmd

# 命令行把 RTT 打到当前终端
.\tmp\rtt\open_rtt_client.cmd
```

`tmp\rtt\jlink\` 里需有 `JLinkRTTViewer.exe` / `JLinkRTTClient.exe`（可从本机 SEGGER 安装目录拷贝，或把脚本里的 `JLINK=` 改成安装路径，例如 `C:\Program Files\SEGGER\JLink`）。

### 方式 B：本机已装 SEGGER J-Link

1. 打开 **J-Link RTT Viewer**（开始菜单或安装目录下的 `JLinkRTTViewer.exe`）
2. 连接参数大致如下：
   - **Connection**：USB
   - **Specify Target Device**：`nRF54L15_M33`（或列表里的 nRF54L15）
   - **Target Interface**：SWD
   - **Speed**：4000 kHz（可先用默认）
3. 通道用 **Terminal 0**（Zephyr `RTT_CONSOLE` 默认 up-buffer 0）
4. 复位或重新上电后应持续刷出 `ink_panel` / `epd:` / `npm1300` 日志

命令行等价：

```powershell
& "C:\Program Files\SEGGER\JLink\JLinkRTTClient.exe"
```

### 注意

| 点 | 说明 |
|----|------|
| 与 UART | `CONFIG_UART_CONSOLE=n`，USB‑TTL 接 UART **看不到** 这些 `printk` |
| 与 nrfutil/west flash | 烧录占用 J-Link 时 Viewer 可能断连；烧完再连 RTT |
| 复位键 | 冷复位会断 RTT；看日志请用 **BTN1 单击** 重刷 demo |
| 无输出 | 查 SWD 线、供电、是否烧的是 `ink_panel`、RTT Viewer 设备名是否选对 |
| 改回 UART 日志 | `CONFIG_RTT_CONSOLE=n` + `CONFIG_UART_CONSOLE=y` 后重编（脚见上表） |

墨水屏：`CONFIG_INK_PANEL_EPD=y`；总线默认 **GPIO bitbang**（对齐 `eink_tag`；`CONFIG_INK_PANEL_EPD_SPIM=n`）。脚：SCK=P1.08 MOSI=P1.09 CS=P1.10 DC=P1.05 RST=P1.06 BUSY=P1.07。实验 SPIM20：`CONFIG_INK_PANEL_EPD_SPIM=y`（当前首次 `spi_write` 会 Bus Fault）。关屏：`CONFIG_INK_PANEL_EPD=n`。

NFC（片内 NFCT T2T）：`CONFIG_INK_PANEL_NFC=y`。期望 RTT `nfc: bringup ok`；手机靠近见 `nfc: field on` / `data read` / `field off`；读到 Text `ink_panel NFC ok`。关改 `n`。
