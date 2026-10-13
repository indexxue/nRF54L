# 交接说明：nRF54L15 `zigbee_demo` → ZigBridge-EM 联调

> **给谁看**：ZigBridge-EM（网关 / ZNP）同事  
> **目的**：说明子设备侧已交付什么、联调场景长什么样、双方怎么测、成功长什么样  
> **对应需求**：[`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md)  
> **子设备工程**：`TY/nRF54L15/projects/zigbee_demo`  
> **日期**：2026-10-13

---

## 1. 一句话结论

nRF54L15 侧已新增独立工程 **`zigbee_demo`**：以 **Zigbee End Device** 角色运行，默认信道 **11**，可用按键入网 / 退网。  
**ZigBridge-EM 侧无需改代码**；按既有 `znp join on` 流程开门即可联调。本期只验收「能 association + End Device Announce」，不做 ZCL / MQTT。

---

## 2. 联调场景（拓扑）

```text
┌─────────────────────────────┐         2.4 GHz Zigbee          ┌─────────────────────────────┐
│  ZigBridge-EM（网关）        │◄──────── Channel 11 ───────────►│  nRF54L15 zigbee_demo      │
│  STM32H743 + E72 ZNP        │         (permit join)           │  End Device（子设备）       │
│  角色：Coordinator          │                                 │  板：eink_tag rev_b 脚     │
│  串口：USART3 @ 115200      │                                 │  串口：UART20 @ 115200     │
└─────────────────────────────┘                                 └─────────────────────────────┘
```

| 侧 | 角色 | 本阶段做什么 |
|----|------|----------------|
| ZigBridge-EM | **Coordinator**（E72 / Z-Stack 3.x） | 建网、`znp join on` 开入网窗；看 join 日志 |
| nRF54L15 `zigbee_demo` | **End Device** | BTN1 搜网入网；BTN2 长按 leave；串口/LED 报状态 |

**非本场景**：nRF 再当 Coordinator；手机 BLE 刷屏；ZCL OnOff 开关灯；MQTT 上报。

---

## 3. 子设备侧交付物（你们需要知道的）

| 项 | 内容 |
|----|------|
| 仓库路径 | `TY/nRF54L15/projects/zigbee_demo` |
| 栈 | Nordic Zigbee R23 add-on（ZBOSS），角色 **End Device** |
| 信道 | 固定 **11**（与网关 `bdbSetChannel(ch11)` 对齐） |
| 操作 | **BTN1 短按** → Network Steering（搜网/入网） |
|  | **BTN2 长按** → leave / 清网络信息（便于反复测） |
| 可观测 | 子设备串口：`zigbee: steering` / `joined nwk=… ieee=…` / `failed` / `idle / left` |
|  | LED：搜网闪 / 已加入常亮 / 失败快闪 / 退网熄 |
| 明确没有 | BLE、墨水屏、PN5180、OnOff/Identify 业务联调、Sleepy ED |

操作说明（烧录/按键）见子设备仓：`projects/zigbee_demo/README.md`。

---

## 4. 双方如何测试（推荐顺序）

### 4.1 前置

| 侧 | 检查 |
|----|------|
| 网关 | E72 为 coordinator hex；USART3 115200；`znp start` 后 `coord=1 st=9` |
| 子设备 | 已烧录 `zigbee_demo`；天线 / 2.4 GHz 正常；与网关同房间近距离先测 |
| 双方 | 信道均为 **11**；子设备若曾入过别的网，先 **BTN2 长按** leave |

### 4.2 入网（主路径）

| # | 谁 | 动作 | 期望 |
|---|----|------|------|
| 1 | 网关 | `znp status` | `coord=1 st=9` |
| 2 | 网关 | `znp join on 180` | `pj>0`（180s 入网窗） |
| 3 | 子设备 | **立刻 BTN1 短按** | 串口 `zigbee: steering`；灯闪 |
| 4 | 网关 | 看 USART3 | 出现 `[ZNP] join nwk=0xXXXX ieee=AABB…` |
| 5 | 子设备 | 看串口 | `zigbee: joined nwk=0xXXXX ieee=…`，**IEEE 与网关一致** |
| 6 | 网关 | `znp join off`；可选 `znp ping` | 关窗；ZNP 仍正常 |

### 4.3 退网再入网（验收 A4）

| # | 谁 | 动作 | 期望 |
|---|----|------|------|
| 1 | 子设备 | **BTN2 长按** | 串口 `zigbee: idle / left` |
| 2 | 网关 | 再 `znp join on 180` | 入网窗打开 |
| 3 | 子设备 | 再 BTN1 短按 | 再次出现 join 日志；IEEE 可对照 |

### 4.4 失败时优先查

1. 网关 permit join 是否仍打开（窗口可能已过）  
2. 信道是否都是 11  
3. 子设备是否仍绑旧 PAN → BTN2 长按 leave 后重试  
4. 距离 / 天线；两板是否都上电  

---

## 5. 成功判定（对齐需求 A1–A4）

| ID | 标准 | 看哪里 |
|----|------|--------|
| A1 | nRF 以 ED 跑 Zigbee | 子设备工程 + 串口有 Zigbee 状态字 |
| A2 | join 窗口内入网 | 网关 `znp join on` 期间完成 |
| A3 | IEEE 一致 | 网关 `[ZNP] join … ieee=` ↔ 子设备 `zigbee: joined … ieee=` |
| A4 | leave 后再入 ≥1 次 | BTN2 长按 → 再走 4.2 |

A5（不影响双方主线）：ZigBridge ZNP 逻辑不变；nRF 的 `eink_tag` BLE 工程未改。

---

## 6. 网关侧命令速查（你们已有）

USART3 @ 115200：

```text
znp status              # coord=1 st=9
znp join on 180         # 开 180s 入网窗
znp join status         # pj>0
# … 子设备 BTN1 …
# 成功示例：
# [ZNP] join nwk=0xXXXX ieee=AABBCCDDEEFF0011
znp join off
znp ping
```

实现位置（网关仓）：`Common/Src/znp_host.c`（本期无需为联调再改）。

---

## 7. 本期边界（避免期望错位）

| 会做 | 不做 |
|------|------|
| ED 入网 / leave / 串口+LED 状态 | ZCL OnOff、Identify 业务互操作 |
| 信道 11 联调 | BLE + Zigbee 共存产品化 |
| 与 ZigBridge Coordinator 互通 | 子设备当 Coordinator；MQTT；设备表持久化；量产证书 |

入网成功后若要做「网关下发开关 / 上报」，属下一阶段，需另开需求。

---

## 8. 联系与路径

| 路径 | 说明 |
|------|------|
| 本文件 | 给 ZigBridge 的联调交接 |
| `TY/nRF54L15/doc/req_nrf54_zigbee_end_device.md` | 原需求 |
| `TY/nRF54L15/projects/zigbee_demo/README.md` | 子设备烧录 / 按键 |
| `TY/ZigBridge-EM/doc/znp_net_acceptance.md` | 网关组网验收（含子设备入网步骤） |

**子设备承接**：nRF54L15 固件（`zigbee_demo`）  
**网关配合**：开 permit join + 核对 join 日志（已具备）
