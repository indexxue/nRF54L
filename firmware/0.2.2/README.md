# firmware 0.2.2 — zigbee_ed + zigbee_router

| 项 | 值 |
|----|-----|
| 版本 | **0.2.2** |
| 打包日 | 20261101 |
| 签名 | unsigned |
| Tag | `v0.2.2` |

## 产物（本机 `nrf.cmd release` 生成，二进制默认不入库）

### zigbee_ed（End Device）

```text
nrf54l15_zigbee_ed_0.2.2_20261101_unsigned.hex   # 优先烧录（merged）
nrf54l15_zigbee_ed_0.2.2_20261101_unsigned.bin
nrf54l15_zigbee_ed_0.2.2_20261101_unsigned.elf
```

```powershell
.\nrf.cmd -Project zigbee_ed release 0.2.2
.\nrf.cmd -Project zigbee_ed flash
```

### zigbee_router（Router）

```text
nrf54l15_zigbee_router_0.2.2_20261101_unsigned.hex
nrf54l15_zigbee_router_0.2.2_20261101_unsigned.bin
nrf54l15_zigbee_router_0.2.2_20261101_unsigned.elf
```

```powershell
.\nrf.cmd -Project zigbee_router release 0.2.2
.\nrf.cmd -Project zigbee_router flash
```

## 本版要点

- 契约 §2.3 同网 rejoin：`parent_unreachable` / `parent_link_fail` / reboot 路径
- 联调 `rx_on_when_idle=1`；ZCL 到站日志 `zigbee: zcl onoff=`
- `zigbee_demo` 工程更名为 `zigbee_ed`
- 网关侧建议同步烧录 AF ACK+路由发现（ZigBridge `ZNP_AF_OPTIONS_DEFAULT`）
