# zigbee_ed

Zigbee **End Device** + **OnOff Server（EP1）** 联调：入网 / 退网 + 网关 `znp light` 控板载绿灯。  
原工程名 **`zigbee_demo`**。当前联调固件 **0.2.3**（见 `firmware/0.2.3/`）。Router 联调见 [`../zigbee_router`](../zigbee_router/)。

板级脚取自 `eink_tag` rev_b（UART / LED×2 / BTN×2）。**无** BLE、EPD、PN5180。

| 层 | 路径 |
|----|------|
| 产品（按键/灯效/日志） | `src/app.c` |
| Zigbee 协议（本工程私有） | `lib/zigbee/` |

- 需求：[`doc/req_nrf54_zigbee_end_device.md`](../../doc/req_nrf54_zigbee_end_device.md)  
- 交付交接：[`doc/zigbee_demo_handoff_to_zigbridge.md`](../../doc/zigbee_demo_handoff_to_zigbridge.md)  
- 系统角色说明：[`README_zigbee_system.md`](README_zigbee_system.md)（Coordinator / Router / ED + 应用类型）  
- **涂鸦标准接入**：计划 [`doc/plan_tuya_zigbee_ed_standard.md`](../../doc/plan_tuya_zigbee_ed_standard.md)；**PID/DP/Basic 身份** 以 [`doc/tuya_ed_zcl_mapping.md`](../../doc/tuya_ed_zcl_mapping.md) 为准（平台字段未填前仅占位联调）  
- **量产边界**：本工程 **禁止** 临时 Coord / `zbcmd` 厂测后门；该类能力仅在 [`projects/factory`](../factory/)（计划 [`doc/plan_zigbee_factory_temp_coord.md`](../../doc/plan_zigbee_factory_temp_coord.md)）

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
| Zigbee 扫描 | 掩码 **11/15/20/25**（`CONFIG_ZIGBEE_CHANNEL_MASK=0x02108800`） |
| Zigbee TX 功率 | `CONFIG_ZIGBEE_APP_TX_POWER`（默认 **+8** dBm；`lib/zigbee/zigbee_app.c` 起栈 apply） |

**MCUBoot** 已接（swap-move，无 FLPR）：slot0=本工程，slot1=factory；`pm_static` 含 `zboss_nvram` + `boot_share`。BTN1 长按 ≈5s → `boot_mode=factory` 进厂测。

## ZCL 契约（对拍网关）

| 项 | 值 |
|----|-----|
| Endpoint | **1**（`ZIGBEE_APP_ENDPOINT`） |
| Cluster | OnOff **`0x0006`** Server |
| On / Off / Toggle | `0x01` / `0x00` / `0x02` |
| Basic Manufacturer / Model | `CONFIG_ZIGBEE_ED_MANUFACTURER` / `CONFIG_ZIGBEE_ED_MODEL`（默认联调占位 `TY` / `zigbee_ed`；见映射表） |
| 串口 | `light: on` / `light: off`；起栈见 `zigbee: id mfr=… model=…` |

## 按键 / LED

| 操作 | 行为 |
|------|------|
| BTN1 短按 | 开启 **60s** 配网窗并 Network Steering（窗内失败会重试） |
| BTN1 长按 ≈5s | `boot_mode=factory` + 冷复位进厂测（须 MCUBoot + slot1 已烧 factory） |
| BTN2 双击 | leave（BDB 本地复位，停在 idle，不自动搜网） |
| BTN2 **长按 3s** | **擦 `zboss_nvram` + 冷复位**（换 Router 镜像前建议做） |

| 状态 | LED | 串口 |
|------|-----|------|
| steering | PAIRING 蓝慢闪 | `zigbee: steering` |
| rejoining | PAIRING | `zigbee: rejoin start …` / `rejoining` / `rejoin ok` |
| joined | SUCCESS 绿/蓝交替 | `zigbee: joined nwk=… ieee=…` |
| failed | ERROR 快闪 | `zigbee: failed` |
| idle / left | TRIGGER 绿闪 | `zigbee: idle / left` |
| ZCL On | LED_G 亮 | `light: on` |
| ZCL Off | LED_G 灭 | `light: off` |

同网 rejoin（契约 [`zigbee_rejoin_contract.md`](../../doc/zigbee_rejoin_contract.md) §2.3）：协调器整机掉电同 ch/pan 后应自动 `rejoin`，无需清本机 NV；未入网仍仅 BTN1 显式入网。

控灯可达（与 Router 对齐）：联调 `rx_on_when_idle=1`；空中到站见 `zigbee: zcl onoff=`；网关需烧录带 AF ACK+路由发现的 ZigBridge；leave 后再入网用**新 nwk** 控灯。

## 构建 / 烧录 / 发布

整包（Boot + APP@slot0 + factory@slot1）：

```powershell
.\nrf.cmd -Project zigbee_ed fullclean
.\nrf.cmd -Project zigbee_ed build-all
.\nrf.cmd -Project zigbee_ed flash              # MCUBoot + zigbee_ed@slot0
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash                # factory → slot1
.\nrf.cmd boot-mode app                         # 默认起 slot0（厂测用 BTN1 长按或 boot-mode factory）
```

日常只改 APP：

```powershell
.\nrf.cmd -Project zigbee_ed build
.\nrf.cmd -Project zigbee_ed flash
.\nrf.cmd -Project zigbee_ed release 0.2.3   # → firmware/0.2.3/
```

刷机前若刚跑过 Router 镜像：建议 BTN2 双击 leave 或擦 `zboss_nvram`，避免角色脏网表。

## 涂鸦标准接入（方案 B）

子设备只认标准 ZCL；云与 App 经**涂鸦 Zigbee 多模网关**。映射与建品填空见 [`doc/tuya_ed_zcl_mapping.md`](../../doc/tuya_ed_zcl_mapping.md)。

**平台侧（T0，需账号）**：IoT 建 **标准 Zigbee** 单路开关灯产品（勿选模组 MCU）→ 把 PID、manufacturer、model、开关 DP 填进映射表 §1 / §3。

**设备侧粗流程（网关到手且映射填齐后）**：

1. 网关配网并在涂鸦智能 / Smart Life 上线  
2. App 添加设备 → 本机 **BTN1** 开 60s 配网窗  
3. 绑定到本 PID 产品后，App 开关 → LED_G + `light: on|off`  
4. 失败：BTN2 双击 leave，或长按擦 `zboss_nvram` 后重来  

PID 未定时固件可用占位身份联调；**量产 release 不得保留占位**。

## ZigBridge-EM 联调

1. 网关：确认 `znp channel`（或清网轮换后记下 ch）→ `znp join on 180`  
2. 本机：BTN1 短按（多信道扫描，可跟上网关 11/15/20/25）→ 对照 IEEE  
3. 网关：`znp light on` / `off` / `toggle` → 看绿灯与 `light: …`  
4. BTN2 双击 leave，再入网 + 再控灯 ≥1 次  
5. （建议）网关长按清网换信道后，子设备 leave 再 steer，确认仍能入网  
6. **同网掉电**：网关整机掉电（勿 clear）→ 本机 `rejoin start`/`rejoin ok` → 再控灯（无需清本机 NV）
