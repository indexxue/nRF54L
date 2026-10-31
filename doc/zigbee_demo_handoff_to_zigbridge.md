# 交付说明：nRF54L15 `zigbee_demo` → ZigBridge-EM

> **给谁看**：ZigBridge-EM（网关 / ZNP）同事  
> **对应需求**：[`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md)（**以该文 ZCL 契约为准**）  
> **子设备工程**：`TY/nRF54L15/projects/zigbee_demo`  
> **交付日期**：2026-10-30  

---

## 1. 一句话结论

nRF54L15 已交付独立联调镜像 **`zigbee_demo`**：

1. **Zigbee End Device**，多信道扫描 **11/15/20/25**（与网关默认掩码对齐），可入网 / leave  
2. **EP1 · OnOff Server `0x0006`**：响应网关 `znp light on|off|toggle`，板载绿灯亮灭，串口打印 `light: on` / `light: off`  
3. **配网行为**：显式入网（60s 窗内重试）/ 显式清网（leave 后不自动搜网）— 见 [`zigbee_demo_pairing_control.md`](zigbee_demo_pairing_control.md)

**ZigBridge-EM 侧无需改代码**。子设备**不解析** USART3 文本，只认空中 ZCL。

| 侧 | 状态 |
|----|------|
| nRF 工程 / 构建 | ✅ `.\nrf.cmd -Project zigbee_demo build` 通过；`merged.hex` 可用 |
| ZCL 契约 | ✅ EP=**1** · Cluster=`0x0006` · On/Off/Toggle |
| 信道扫描 | ✅ 掩码 `0x02108800`（11/15/20/25） |
| 网关代码 | ✅ 无需改（`znp_host_zcl_onoff` + CLI 已具备） |
| 实机入网+控灯 | ✅ 已对拍（leave→steer→joined；建议再验换信道 / light） |

---

## 2. 拓扑

```text
┌─────────────────────────────┐         Ch11 Zigbee          ┌─────────────────────────────┐
│  ZigBridge-EM（Coordinator） │◄────────────────────────────►│  nRF54L15 zigbee_demo (ED) │
│  znp join / znp light …     │   ZCL OnOff → EP1 Server     │  绿灯 = 灯输出              │
│  USART3 @ 115200            │                              │  UART20 @ 115200            │
└─────────────────────────────┘                              └─────────────────────────────┘
```

---

## 3. 子设备交付物

| 项 | 内容 |
|----|------|
| 路径 | `TY/nRF54L15/projects/zigbee_demo` |
| 角色 | End Device（非 Coordinator） |
| 信道 | 扫描掩码 **11/15/20/25**（`CONFIG_ZIGBEE_CHANNEL_MASK=0x02108800`） |
| Endpoint | **1** |
| Cluster | OnOff **`0x0006`** Server |
| 本地输出 | 板载 **LED_G（P2.08）** 亮/灭 |
| 串口日志 | `zigbee: joined nwk=… ieee=…`；`light: on` / `light: off` |
| 入网 | **BTN1 短按** → Network Steering |
| 退网 | **BTN2 双击** → leave / 清网 |
| 网络灯效 | 搜网蓝闪 / 成功绿蓝交替 / 失败快闪 / 退网绿灯闪 |
| 明确没有 | BLE、EPD、Level、MQTT、解析 `znp light` ASCII |
| 对 BLE 主线 | **未改** `eink_tag` / `demo` / `factory` |

### 烧录

```powershell
.\nrf.cmd -Project zigbee_demo build
.\nrf.cmd -Project zigbee_demo flash
```

依赖：`NCS_ZIGBEE_ROOT` → ncs-zigbee v1.4.0（见工程 README）。

---

## 4. 联调步骤（对齐需求 §5）

| # | 谁 | 动作 | 期望 |
|---|----|------|------|
| 1 | 网关 | `znp status`；记下 `znp channel`（或清网轮换后的 ch） | `coord=1 st=9`；ch ∈ {11,15,20,25} |
| 2 | 子设备 | 烧录；必要时 BTN2 双击清旧网 | 就绪 |
| 3 | 网关 | `znp join on 180` | `pj>0` |
| 4 | 子设备 | **立刻 BTN1 短按** | `steering (mask=0x02108800…)` → `joined nwk=… ieee=…` |
| 5 | 双方 | 对照 IEEE | 与 `[ZNP] join … ieee=` **一致** |
| 6 | 网关 | `znp join off` | 关窗 |
| 7 | 网关 | `znp light on` → `off` → `toggle` | 绿灯亮/灭；串口 `light: on` / `light: off` |
| 8 | 网关 | `znp ping` | 回归 |
| 9 | 双方 | BTN2 双击 leave → 再入网 → 再控灯 | A4 + 控灯仍可用 |
| 10 | 建议 | 网关清网换信道后再测 leave→steer | 子设备多信道扫描仍能入网 |

### 网关控灯命令（已有）

```text
znp light on               # 最近 join，ep=1
znp light off
znp light toggle
znp light on 0xXXXX 1      # 指定 nwk + EP
```

---

## 5. 验收对照（A1–A8）

| ID | 标准 | 软件侧 | 实机 |
|----|------|--------|------|
| A1 | ED 跑 Zigbee | ✅ | — |
| A2 | join 窗口内入网 | ✅ | ✅ 已对拍 |
| A3 | IEEE 一致 | ✅ | ✅ 已对拍 |
| A4 | leave 后再入 ≥1 | ✅（BTN2 双击） | ✅ 已对拍 |
| A5 | 不影响主线 | ✅ | — |
| **A6** | `znp light on` → 灯亮 + on 日志 | ✅ 已实现 | ✅ / 按现场复核 |
| **A7** | `off` 灭；`toggle` 翻转 | ✅ 已实现 | ✅ / 按现场复核 |
| A8 | 掉电仍在网可控灯（建议） | NV 由 ZBOSS 保持 | ⏳ 建议补测 |
| 多信道 | 掩码含 11/15/20/25 | ✅ | ✅ / 建议换 ch 再验 |

---

## 6. 失败优先查

1. permit join 是否过期  
2. 网关信道是否在子设备掩码内（**11/15/20/25**）  
3. 旧网残留 → BTN2 双击 leave  
4. 网关是否打到 **EP1**（默认即可）  
5. 子设备是否 OnOff **Server**（本镜像是）  
6. 距离 / 天线  

配网状态机细节：[`zigbee_demo_pairing_control.md`](zigbee_demo_pairing_control.md)。

---

## 7. 本期边界

| 会做 | 不做 |
|------|------|
| 入网 + EP1 OnOff 控灯 | Level / Identify 业务验收 |
| 多信道扫描（11/15/20/25）联调 | BLE+Zigbee 共存；MQTT；证书 |
| 标准 ZCL 空中协议 | 解析网关 CLI 文本 |

下一阶段（MQTT / 设备表等）另开需求。

---

## 8. 路径

| 路径 | 说明 |
|------|------|
| 本文件 | 交付 / 联调交接 |
| `doc/req_nrf54_zigbee_end_device.md` | 正式需求（契约） |
| `doc/zigbee_demo_pairing_control.md` | 配网状态机 / 60s 窗 / 多信道 |
| `projects/zigbee_demo/README.md` | 烧录 / 按键 / EP / 掩码 |
| ZigBridge `Common/Src/znp_host.c` | `znp_host_zcl_onoff`（网关已具备） |

**子设备承接**：nRF54L15 `zigbee_demo`  
**网关配合**：`znp join on` + `znp light …`（已具备）
