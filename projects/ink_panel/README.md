# ink_panel

EVT 自有板。无线链路为 **BLE Peripheral**（仓库 [`ble/`](../../ble/)），支持 **EPD 平面上传**（对齐 `eink_tag` / [`ble/docs/display_plane_upload.md`](../../ble/docs/display_plane_upload.md)）。

**进度页入口** → [`tmp/ink_panel/index.html`](../../tmp/ink_panel/index.html)  
- 最终产品终端 → [`product.html`](../../tmp/ink_panel/product.html)  
- 当前所处阶段 → [`stage.html`](../../tmp/ink_panel/stage.html)  
- 文字说明 → [`PRODUCT.md`](../../tmp/ink_panel/PRODUCT.md)  
- **联调工具（GUI）** → [`tools/ink_panel_console`](../../tools/ink_panel_console)：`python main.py`（关联 [`nrf_console`](../../tools/nrf_console)）

| 层 | 路径 |
|----|------|
| 产品（按键/CMD/会话） | `src/app.c` |
| BLE 协议（仓库共享） | `ble/` via `modules/nrf54_ble` |
| EPD 胶水 | `board/epd_board.*` + `cbb/epd` |
| 进度与终态预览 | `tmp/ink_panel/` |

本轮（α）：W25Q **页槽**（home/notes/info）+ 开机刷 slot0；BLE REFRESH 写入 home/0。  
本轮不做：NFC 业务、字库描字、CAP1203（γ）、BEGIN mode/page（β）。

## 板级（EVT 网表）

| 功能 | 网名 | 引脚 |
|------|------|------|
| UART TX | `UART_TX` | **P2.02**（手册专用 UARTE TX；**不是**原理图误标的 P2.01） |
| UART RX | `UART_RX` | **P2.00**（UARTE RX；外接 TTL TX；只看日志可不接） |
| LED_STATUS | `LED_STATUS_N` | **P1.13** ACTIVE_LOW |
| BTN1 | `BTN_USER_N` | **P0.00** |
| BTN2 | — | **未接**（勿占用 `TOUCH_ALERT` P0.01） |
| BLE 2.4G | — | 片上 RADIO（设备名 `ink_panel`） |
| EPD | — | SCK=P1.08 MOSI=P1.09 CS=P1.10 DC=P1.05 RST=P1.06 BUSY=P1.07 |
| W25Q64 | — | SPIM00：SCK=P2.06 MOSI=P2.08 MISO=P2.09 CS=P2.10（字库/图形预留） |

## 按键

| 操作 | 行为 |
|------|------|
| BTN1 **单击** | 已连接 → **断连**（随后自动再广播）；未连接 → **开广播**以便重连 |
| BTN1 **双击** | 预留（2.9" 已去掉） |
| BTN1 长按 ≈5s | `boot_mode=factory` + 冷复位（须 slot1 已烧 factory） |

刷屏在 EPD 线程；main 只入队 / 写平面。勿用复位键看日志（会断 J-Link RTT）。  
产品翻页/切模：触摸 CAP1203（γ）。实验室仍可用 BLE `DISPLAY(2001)` 入队 demo。

## 面板

仅 **4.2" BWR**（WS 4.2" B V2，400×300，平面 15000）。NVS `epanel` 历史值忽略。

## BLE（nRF Connect）

自定义 128-bit 服务（前 32 bit = FNV-1a(NVS sn)），见 [`ble/README.md`](../../ble/README.md)。  
平面协议细节见 [`ble/docs/display_plane_upload.md`](../../ble/docs/display_plane_upload.md)（文档示例多为 2.9；本板默认按 **4.2 / 15000** 传）。

| 步骤 | 期望 |
|------|------|
| 扫描连接 `ink_panel`，对 TX **Enable Notify** | RTT `ble: connected` |
| RX Write：`E9 03 00 00`（PING 1001） | Notify ACK；RTT `app: PING ok` |
| 保持连接约 20s | HEARTBEAT Notify；RTT `app: HEARTBEAT` |
| DISPLAY(2001) | 入队本地 demo；ACK=0 |
| BEGIN/CHUNK/REFRESH（2002–2004） | 按 4.2" 上传；CRC 后 **写入 Flash home/0 并刷屏**（α） |
| 刷屏中再发 PING | 仍有 ACK |
| 刷屏中再 BEGIN/CHUNK/REFRESH | ACK `STATUS_INVALID_STATE` |

**不要用** nRF Connect 的 SMP / DFU / UART 菜单测业务。

### 4.2" 上传尺寸（默认）

- `width=400`，`height=300`
- 含 BW/RED 时对应 `len=15000`，CRC32 为整平面

### 2.9" 上传尺寸

双击切到 2.9 并复位后：

- `width=128`，`height=296`，`len=4736`

## W25Q 页槽（α）

| 区 | 说明 |
|----|------|
| meta @0x0 | 4KB：magic/valid 位图 |
| home×1 + notes×8 + info×4 | 各 32KB；空槽刷白 |
| font_cjk @0x69000 | 2MB 预留（不读） |

开机：`page_slot` 加载 **home/0** 刷屏。Host `REFRESH`：RAM 帧 → 存 home/0 → 刷屏。复位后再开机应仍见上次画面。细节见 [`tmp/ink_panel/PRODUCT.md`](../../tmp/ink_panel/PRODUCT.md)。

## 构建 / 烧录

```powershell
.\nrf.cmd -Project ink_panel fullclean
.\nrf.cmd -Project ink_panel build-all
.\nrf.cmd -Project ink_panel flash

# 日常只改 APP：
.\nrf.cmd -Project ink_panel build
.\nrf.cmd -Project ink_panel flash
```

期望 RTT：`ink_panel boot` / `ink_panel ok board=...`；异常才有 `epd:` / `npm1300:` 失败行。

## 日志（J-Link RTT）

本工程 **默认不用 UART 控制台**，`printk` 走 **SEGGER RTT**（见 `prj.conf`）：

```conf
CONFIG_USE_SEGGER_RTT=y
CONFIG_RTT_CONSOLE=y
CONFIG_UART_CONSOLE=n
```

须 **J-Link 接好 SWD 且目标在跑**（可先 `flash` 再开 RTT；Viewer 与烧录不要抢同一把锁太久）。

### 方式 A：仓库脚本（推荐）

```powershell
.\tmp\rtt\open_rtt_viewer.cmd
.\tmp\rtt\open_rtt_client.cmd
```

### 方式 B：本机已装 SEGGER J-Link

1. 打开 **J-Link RTT Viewer**
2. Target：`nRF54L15_M33`，SWD
3. 通道 **Terminal 0**

### 注意

| 点 | 说明 |
|----|------|
| 与 UART | `CONFIG_UART_CONSOLE=n`，USB‑TTL **看不到** `printk` |
| 烧录 | 占用 J-Link 时 Viewer 可能断连；烧完再连 |
| 复位键 | 冷复位会断 RTT；BTN1 单击用于断连/再广播 |
| BLE | 须 `CONFIG_START_REQUEST_HFXO=y`（已开） |

墨水屏：`CONFIG_INK_PANEL_EPD=y`；总线默认 **GPIO bitbang**（`CONFIG_INK_PANEL_EPD_SPIM=n`）。实验 SPIM20 当前首次 `spi_write` 会 Bus Fault。

NFC（片内 NFCT）：`CONFIG_INK_PANEL_NFC` 默认工程关；手机验读不在本轮。
