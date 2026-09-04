# eink_tag

通过 **片上 NFC（NFCT）或 BLE** 刷墨水屏标签（后续）。  
**当前冒烟**：内部 RC + UART + LED_G/LED_B + BTN1/BTN2（NFC/BLE/EPD 暂关）。

## 当前接线

| 功能 | 引脚 |
|------|------|
| UART TX / RX | **P1.09** / **P1.10** |
| LED_G / LED_B | **P1.05** / **P1.04**（ACTIVE_LOW） |
| BTN1 / BTN2 | **P0.00** / **P0.01**（ACTIVE_LOW） |
| 时钟 | 内部 LFCLK RC |

`board` mask：`UART | LED | BUTTON`

```powershell
.\nrf.cmd -Project eink_tag build
.\nrf.cmd -Project eink_tag flash
```

串口 115200：应见 heartbeat、BOOTUP 灯效；按键打 `button` 日志。
