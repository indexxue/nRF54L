# zigbee_demo

Zigbee **End Device** 联调：入网 / 退网 + LED / 按键。  
板级脚取自 `eink_tag` rev_b（UART / LED×2 / BTN×2）。**无** BLE、EPD、PN5180。

- 需求：[`doc/req_nrf54_zigbee_end_device.md`](../../doc/req_nrf54_zigbee_end_device.md)  
- 设计：[`docs/superpowers/specs/2026-10-13-zigbee-demo-design.md`](../../docs/superpowers/specs/2026-10-13-zigbee-demo-design.md)

## 依赖

| 项 | 说明 |
|----|------|
| 本仓默认 NCS | `NCS_ROOT` → **v3.2.0**（GCC 12） |
| Zigbee R23 add-on | `NCS_ZIGBEE_ROOT` → [ncs-zigbee](https://github.com/nrfconnect/ncs-zigbee) **v1.4.0**（官方配对 NCS **v3.4.0**） |

在本仓 **NCS 3.2** 上可编过：使用 add-on 的 **trace / 非 LTO** 预编译库（`CONFIG_ZIGBEE_ENABLE_TRACES=y`）。正式联调若遇栈行为异常，建议按官方安装 **NCS 3.4.x + Zigbee add-on**，本工程只改 `nrf.env` 的 `NCS_*`，**不要**动 `eink_tag` BLE 路径。

```powershell
git clone --branch v1.4.0 https://github.com/nrfconnect/ncs-zigbee.git D:\ncs\tmp\ncs-zigbee
# nrf.env:
# NCS_ZIGBEE_ROOT=D:\ncs\tmp\ncs-zigbee
```

## 板级（rev_b）

| 功能 | 引脚 |
|------|------|
| UART TX / RX | P1.04 / P1.05 |
| LED_G / LED_B | P2.08 / P2.09（ACTIVE_LOW） |
| BTN1 / BTN2 | P0.00 / P0.01（ACTIVE_LOW） |
| Zigbee 信道 | **11**（`CONFIG_ZIGBEE_CHANNEL`） |

本工程 **无 MCUBoot / 无 FLPR**；`pm_static.yml` 含 `zboss_nvram`。

## 按键 / LED

| 操作 | 行为 |
|------|------|
| BTN1 短按 | Network Steering |
| BTN2 长按 | leave（`zb_bdb_reset_via_local_action`） |

| 状态 | LED | 串口 |
|------|-----|------|
| steering | PAIRING 闪 | `zigbee: steering` |
| joined | SUCCESS | `zigbee: joined nwk=… ieee=…` |
| failed | ERROR | `zigbee: failed` |
| idle / left | 熄 | `zigbee: idle / left` |

## 构建 / 烧录

```powershell
.\nrf.cmd -Project zigbee_demo build
.\nrf.cmd -Project zigbee_demo flash
```

## ZigBridge-EM 联调

给网关侧的完整交接（场景 / 步骤 / 验收）：  
[`doc/zigbee_demo_handoff_to_zigbridge.md`](../../doc/zigbee_demo_handoff_to_zigbridge.md)

1. 网关：`znp start` → `znp join on 180`  
2. 本机：BTN1 短按  
3. 网关 `[ZNP] join …` 与本机 IEEE 对照  
4. BTN2 长按 leave，再入网 ≥1 次  
