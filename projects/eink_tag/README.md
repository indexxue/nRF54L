# eink_tag

通过 **片上 NFC（NFCT）或 BLE** 刷墨水屏标签（后续）。  
**当前冒烟**：HFXO + UART + LED + 按键 + **BLE 广播/CMD** + **2.9" BWR EPD**。NFC 暂关。

平台起机：[`start/`](../../start/)（`CONFIG_START_REQUEST_HFXO=y`）。本工程 `src/app.c` 只做 BLE + EPD。

## 当前接线

| 功能 | 引脚 |
|------|------|
| UART TX / RX | **P1.09** / **P1.10** |
| LED_G / LED_B | **P1.05** / **P1.04**（ACTIVE_LOW） |
| BTN1 / BTN2 | **P0.00** / **P0.01**（ACTIVE_LOW） |
| EPD SCK / MOSI / CS | **P2.01** / **P2.04** / **P2.05**（SPI00） |
| EPD DC / RST / BUSY | **P1.08** / **P1.07** / **P1.06** |
| BLE 2.4G | 片上 RADIO（设备名 `eink_tag`） |
| HFCLK | 外部 **32 MHz HFXO** |
| LFCLK | 内部 RC |

`board` mask：`UART | LED | BUTTON | SPI | BLE`

BLE 协议在仓库 [`ble/`](../../ble/)（`modules/nrf54_ble`）。  
- 下行测试：`1001` PING → 仅 ACK，**不刷屏**；字节 `E9 03 00 00`  
- 下行刷屏：`2001` DISPLAY → **异步全刷 demo**（payload 暂忽略）；字节 `D1 07 00 00`  
- 上行心跳：`8001` HEARTBEAT（约 **20s**，需 TX Notify）  

等串口出现 `epd: ready` 后再发 DISPLAY；刷屏期间灯效/BLE 不停。  

**不要用** nRF Connect 的 SMP / DFU / UART 菜单（会报 `SMP service not found`）。用自定义 128-bit 服务：

| 角色 | UUID |
|------|------|
| Service | `53530120-0001-1000-8000-00805f9b34fb` |
| RX（手机→设备 Write） | `53530120-0002-1000-8000-00805f9b34fb` |
| TX（设备→手机 Notify） | `53530120-0003-1000-8000-00805f9b34fb` |

nRF Connect 步骤：

1. 连接 **`eink_tag`** → 点 **Unknown Service**（上表 Service UUID）  
2. 对 **TX**（`…0003…`）点三下箭头 / Enable **Notify**  
3. 对 **RX**（`…0002…`）Write，BYTE ARRAY：  
   - 链路测试 PING：`E9030000`（或 `E9 03 00 00`）  
   - **刷墨水屏 DISPLAY**：`D1070000`（或 `D1 07 00 00`）＝ cmd **2001**  

```powershell
.\nrf.cmd -Project eink_tag build
.\nrf.cmd -Project eink_tag flash
```

扫描 **`eink_tag`**；串口应见 `injected BLE_CMD_PING`；连上并开 Notify 后见 ACK / HEARTBEAT。
