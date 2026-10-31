# 交付说明：nRF54L15 `zigbee_demo` → ZigBridge-EM

> **给谁看**：ZigBridge-EM（网关 / ZNP）同事  
> **对应需求**：[`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md)（**以该文 ZCL 契约为准**）  
> **子设备工程**：`TY/nRF54L15/projects/zigbee_demo`  
> **交付日期**：2026-10-30  

---

## 1. 一句话结论

nRF54L15 已交付独立联调镜像 **`zigbee_demo`**：

1. **Zigbee End Device**，信道含 **11**，可入网 / leave  
2. **EP1 · OnOff Server `0x0006`**：响应网关 `znp light on|off|toggle`，板载绿灯亮灭，串口打印 `light: on` / `light: off`  

**ZigBridge-EM 侧无需改代码**。子设备**不解析** USART3 文本，只认空中 ZCL。

| 侧 | 状态 |
|----|------|
| nRF 工程 / 构建 | ✅ `.\nrf.cmd -Project zigbee_demo build` 通过；`merged.hex` 可用 |
| ZCL 契约 | ✅ EP=**1** · Cluster=`0x0006` · On/Off/Toggle |
| 网关代码 | ✅ 无需改（`znp_host_zcl_onoff` + CLI 已具备） |
| 实机入网+控灯 | ⏳ 待同场对拍（A2–A4、A6–A7） |

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
| 信道 | 固定 **11**（`CONFIG_ZIGBEE_CHANNEL`；量产再扩掩码） |
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
| 1 | 网关 | `znp status` | `coord=1 st=9` |
| 2 | 子设备 | 烧录；必要时 BTN2 双击清旧网 | 就绪 |
| 3 | 网关 | `znp join on 180` | `pj>0` |
| 4 | 子设备 | **立刻 BTN1 短按** | `zigbee: steering` → `joined nwk=… ieee=…` |
| 5 | 双方 | 对照 IEEE | 与 `[ZNP] join … ieee=` **一致** |
| 6 | 网关 | `znp join off` | 关窗 |
| 7 | 网关 | `znp light on` → `off` → `toggle` | 绿灯亮/灭；串口 `light: on` / `light: off` |
| 8 | 网关 | `znp ping` | 回归 |
| 9 | 双方 | BTN2 双击 leave → 再入网 → 再控灯 | A4 + 控灯仍可用 |

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
| A2 | join 窗口内入网 | 就绪 | ⏳ |
| A3 | IEEE 一致 | 就绪 | ⏳ |
| A4 | leave 后再入 ≥1 | 就绪（BTN2 双击） | ⏳ |
| A5 | 不影响主线 | ✅ | — |
| **A6** | `znp light on` → 灯亮 + on 日志 | ✅ 已实现 | ⏳ |
| **A7** | `off` 灭；`toggle` 翻转 | ✅ 已实现 | ⏳ |
| A8 | 掉电仍在网可控灯（建议） | NV 由 ZBOSS 保持 | ⏳ |

---

## 6. 失败优先查

1. permit join 是否过期  
2. 信道是否同为 **11**  
3. 旧网残留 → BTN2 双击 leave  
4. 网关是否打到 **EP1**（默认即可）  
5. 子设备是否 OnOff **Server**（本镜像是）  
6. 距离 / 天线  

---

## 7. 本期边界

| 会做 | 不做 |
|------|------|
| 入网 + EP1 OnOff 控灯 | Level / Identify 业务验收 |
| 信道 11 联调 | BLE+Zigbee 共存；MQTT；证书 |
| 标准 ZCL 空中协议 | 解析网关 CLI 文本 |

下一阶段（MQTT / 设备表等）另开需求。

---

## 8. 路径

| 路径 | 说明 |
|------|------|
| 本文件 | 交付 / 联调交接 |
| `doc/req_nrf54_zigbee_end_device.md` | 正式需求（契约） |
| `projects/zigbee_demo/README.md` | 烧录 / 按键 / EP |
| ZigBridge `Common/Src/znp_host.c` | `znp_host_zcl_onoff`（网关已具备） |

**子设备承接**：nRF54L15 `zigbee_demo`  
**网关配合**：`znp join on` + `znp light …`（已具备）
