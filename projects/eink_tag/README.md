# eink_tag

通过 **片上 NFC（NFCT）或 BLE** 刷墨水屏标签（后续）。  
**当前冒烟**：内部 RC + UART + LED + 按键 + **2.9" 红黑白 EPD**（GDEY029Z95）。

## 当前接线

| 功能 | 引脚 |
|------|------|
| UART TX / RX | **P1.09** / **P1.10** |
| LED_G / LED_B | **P1.05** / **P1.04**（ACTIVE_LOW） |
| BTN1 / BTN2 | **P0.00** / **P0.01**（ACTIVE_LOW） |
| EPD SCK / MOSI / CS | **P2.01** / **P2.04** / **P2.05**（SPI00） |
| EPD DC / RST / BUSY | **P1.08** / **P1.07** / **P1.06** |
| 时钟 | 内部 LFCLK RC |

`board` mask：`UART | LED | BUTTON | SPI`

启动后自动跑 EPD 测试：黑 → 白 → 红 → 三色条纹全刷。

```powershell
.\nrf.cmd -Project eink_tag build
.\nrf.cmd -Project eink_tag flash
```

串口 115200：应见 `epd: ready`、`epd: test done`、heartbeat；屏上显示 BWR 色条。
