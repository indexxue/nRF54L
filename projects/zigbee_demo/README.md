# zigbee_demo

Zigbee **End Device** + **OnOff Server（EP1）** 联调：入网 / 退网 + 网关 `znp light` 控板载绿灯。  
板级脚取自 `eink_tag` rev_b（UART / LED×2 / BTN×2）。**无** BLE、EPD、PN5180。

| 层 | 路径 |
|----|------|
| 产品（按键/灯效/日志） | `src/app.c` |
| Zigbee 协议（本工程私有） | `lib/zigbee/` |

- 需求：[`doc/req_nrf54_zigbee_end_device.md`](../../doc/req_nrf54_zigbee_end_device.md)  
- 交付交接：[`doc/zigbee_demo_handoff_to_zigbridge.md`](../../doc/zigbee_demo_handoff_to_zigbridge.md)

## 依赖

| 项 | 说明 |
|----|------|
| 本仓默认 NCS | `NCS_ROOT` → **v3.2.0**（GCC 12） |
| Zigbee R23 add-on | `NCS_ZIGBEE_ROOT` → [ncs-zigbee](https://github.com/nrfconnect/ncs-zigbee) **v1.4.0** |

在本仓 **NCS 3.2** 上可编过：`CONFIG_ZIGBEE_ENABLE_TRACES=y`（非 LTO 库）。异常时建议 NCS 3.4.x + 同 add-on，只改 `nrf.env` 的 `NCS_*`，**不要**动 `eink_tag`。

```powershell
git clone --branch v1.4.0 https://github.com/nrfconnect/ncs-zigbee.git D:\ncs\tmp\ncs-zigbee
# nrf.env: NCS_ZIGBEE_ROOT=D:\ncs\tmp\ncs-zigbee
```

## 板级（rev_b）

| 功能 | 引脚 |
|------|------|
| UART TX / RX | P1.04 / P1.05 |
| LED_G / LED_B | P2.08 / P2.09（ACTIVE_LOW）；**控灯用 LED_G** |
| BTN1 / BTN2 | P0.00 / P0.01（ACTIVE_LOW） |
| Zigbee 信道 | **11**（`CONFIG_ZIGBEE_CHANNEL`） |

无 MCUBoot / 无 FLPR；`pm_static.yml` 含 `zboss_nvram`。

## ZCL 契约（对拍网关）

| 项 | 值 |
|----|-----|
| Endpoint | **1**（`ZIGBEE_APP_ENDPOINT`） |
| Cluster | OnOff **`0x0006`** Server |
| On / Off / Toggle | `0x01` / `0x00` / `0x02` |
| 串口 | `light: on` / `light: off` |

## 按键 / LED

| 操作 | 行为 |
|------|------|
| BTN1 短按 | 开启 **60s** 配网窗并 Network Steering（窗内失败会重试） |
| BTN2 双击 | leave（清网络，停在 idle，不自动搜网） |

| 状态 | LED | 串口 |
|------|-----|------|
| steering | PAIRING 蓝慢闪 | `zigbee: steering` |
| joined | SUCCESS 绿/蓝交替 | `zigbee: joined nwk=… ieee=…` |
| failed | ERROR 快闪 | `zigbee: failed` |
| idle / left | TRIGGER 绿闪 | `zigbee: idle / left` |
| ZCL On | LED_G 亮 | `light: on` |
| ZCL Off | LED_G 灭 | `light: off` |

## 构建 / 烧录

```powershell
.\nrf.cmd -Project zigbee_demo build
.\nrf.cmd -Project zigbee_demo flash
```

## ZigBridge-EM 联调

1. 网关：`znp start` → `znp join on 180`  
2. 本机：BTN1 短按 → 对照 IEEE  
3. 网关：`znp light on` / `off` / `toggle` → 看绿灯与 `light: …`  
4. BTN2 双击 leave，再入网 + 再控灯 ≥1 次  
