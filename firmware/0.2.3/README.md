# firmware 0.2.3 — zigbee_ed + zigbee_router

| 项 | 值 |
|----|-----|
| 版本 | **0.2.3** |
| 打包日 | 20261102 |
| 签名 | unsigned |
| Tag | `v0.2.3` |

## 产物（本机 `nrf.cmd release` 生成，二进制默认不入库）

### zigbee_ed（End Device）

```text
nrf54l15_zigbee_ed_0.2.3_20261102_unsigned.hex   # 优先烧录（merged = MCUBoot + APP）
nrf54l15_zigbee_ed_0.2.3_20261102_unsigned.bin
nrf54l15_zigbee_ed_0.2.3_20261102_unsigned.elf
```

```powershell
.\nrf.cmd -Project zigbee_ed release 0.2.3
.\nrf.cmd -Project zigbee_ed flash
.\nrf.cmd -Project factory flash          # slot1 厂测
.\nrf.cmd boot-mode app
```

### zigbee_router（Router）

```text
nrf54l15_zigbee_router_0.2.3_20261102_unsigned.hex
nrf54l15_zigbee_router_0.2.3_20261102_unsigned.bin
nrf54l15_zigbee_router_0.2.3_20261102_unsigned.elf
```

```powershell
.\nrf.cmd -Project zigbee_router release 0.2.3
.\nrf.cmd -Project zigbee_router flash
```

## 本版要点

- **MCUBoot**：ed/router 双槽布局；slot0=APP，slot1=factory；`boot_share` 旁路
- **BTN1 长按 ≈5s** → `boot_mode=factory` 进厂测（须 slot1 已烧 factory）
- **TX 功率**：`CONFIG_ZIGBEE_APP_TX_POWER`（默认 +8 dBm），`lib/zigbee/zigbee_app.c` 起栈 apply
- 厂测侧：`zb pwr` / `zb rssi`，以及可空中代理的 `pwr` / `rssi`
