# firmware 0.2.1 — zigbee_ed（打包时工程名仍为 zigbee_demo）

| 项 | 值 |
|----|-----|
| 工程（现名） | `projects/zigbee_ed`（由 `zigbee_demo` 改名） |
| 版本 | **0.2.1** |
| 角色 | Zigbee End Device + OnOff Server（EP1） |
| 打包日 | 20261031 |
| 签名 | unsigned |

## 产物（本机 `nrf.cmd release` 生成，二进制默认不入库）

```text
nrf54l15_zigbee_demo_0.2.1_20261031_unsigned.hex   # 优先烧录（merged；文件名保留旧工程名）
nrf54l15_zigbee_demo_0.2.1_20261031_unsigned.bin
nrf54l15_zigbee_demo_0.2.1_20261031_unsigned.elf
```

```powershell
.\nrf.cmd -Project zigbee_ed release 0.2.1
.\nrf.cmd -Project zigbee_ed flash
```

Router 联调工程：`projects/zigbee_router`（另版发布）。

## 本版要点

- 多信道扫描 11/15/20/25；BTN1 配网窗 / BTN2 leave
- EP1 OnOff `0x0006` Server，对拍 ZigBridge `znp light`
- 系统角色说明见 `projects/zigbee_ed/README_zigbee_system.md`
