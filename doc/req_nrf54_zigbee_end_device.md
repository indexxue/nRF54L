# 需求说明：nRF54L15 Zigbee 子设备适配（转交用）

> **文档用途**：跨仓库交接 / 需求传递（ZigBridge-EM → nRF54L15）。请子设备同事**按本文契约实现**，不必解析网关串口文本。  
> **产品背景**：按**量产上市**路径开发（用户家配网 → 控灯；日后 MQTT 走同一空中协议）。当前可用 USART3 CLI 代替 App 触发，空中仍是标准 ZCL。  
> **全流程总览**（可选阅读）：[`product_full_process_guide.md`](product_full_process_guide.md)  
> **状态日期**：2026-10-30  

---

## 0. 给子设备同事的一页摘要

| 项 | 内容 |
|----|------|
| 你的角色 | Zigbee **End Device**（禁止再做 Coordinator） |
| 网关 | `TY/ZigBridge-EM`：H743 + E72（Z-Stack 3.x **Coordinator** / ZNP） |
| 本期必须交付 | ① 能入网 ② 能响应网关下发的 **ZCL OnOff**（亮/灭灯或板载 LED） |
| 空中契约 | EP **1** · Cluster **`0x0006`** · On=`0x01` / Off=`0x00` / Toggle=`0x02` |
| 你不需要做 | 解析 `znp light on` 字符串；实现 MQTT；改网关仓 |
| 信道 | 子设备扫描掩码 **11/15/20/25**（`0x02108800`，对齐网关 M5）；须与协调器当前 ch 同号 |
| 网关已具备 | `znp join on` 入网；`znp light on\|off\|toggle` 发 ZCL（`znp_host_zcl_onoff`） |

**成功一句话**：网关 USART3 先见 `[ZNP] join …`，再敲 `znp light on` 后，你的灯/LED 变亮，且串口打印类似 `light: on`。

---

## 1. 背景与目标

| 项 | 内容 |
|----|------|
| 网关工程 | `TY/ZigBridge-EM`（与 `TY/nRF54L15` **同级**） |
| 子设备工程 | 建议 `projects/zigbee_demo` / `zigbee_ed`（**勿破坏**现有 BLE 量产路径） |
| 网关已完成 | ZNP ping、协调器建网、permit join、**AF/ZCL OnOff 发送 + CLI** |
| 本需求目标 | nRF54L15 作 ED：**入网 + OnOff Server 控灯**，与网关对拍 |
| 非目标（本期） | MQTT、设备表持久化（网关侧）、量产证书、BLE+Zigbee 长期共存产品化、Level 调光 |

入网验收可参考：[`znp_net_acceptance.md`](znp_net_acceptance.md)。  
联调交接补充：[`zigbee_demo_handoff_to_zigbridge.md`](zigbee_demo_handoff_to_zigbridge.md)（若与本文冲突，**以本文 ZCL 契约为准**）。

---

## 2. 可行性与现状

### 2.1 芯片 / SDK

nRF54L15 官方支持 Zigbee ED / Router（Zigbee R23 add-on）。推荐以 **Light bulb / OnOff Server** 类样例为底（需 **Server** 收 On/Off；纯 Light switch Client 不够控灯验收）。

参考：

- [Software maturity（Zigbee on nRF54L15）](https://nrfconnectdocs.nordicsemi.com/addons/ncs-matter-zigbee/latest/software-maturity.html)
- [Zigbee samples（add-on）](https://nrfconnectdocs.nordicsemi.com/addons/ncs-zigbee/latest/samples/light_switch/README.html)

### 2.2 互操作预期

| 侧 | 栈 / 角色 |
|----|-----------|
| ZigBridge-EM | Host：`znp_host`；射频：E72 **Coordinator**（已测 `cap=0x0659` 类 Koenkk 镜像） |
| nRF54L15 | ZBOSS / R23，**End Device** + **OnOff Server** |

跨栈入网与标准 ZCL OnOff 为预期路径。优先排查：信道、旧网残留、permit join 窗口、EP/Cluster 不一致。

### 2.3 仓库现状

| 仓库 | 现状 |
|------|------|
| ZigBridge-EM | 入网 + `znp light` **已实现**；待与子设备对拍 |
| nRF54L15 | 主线多为 BLE；需 **Zigbee ED + OnOff** 联调镜像（可已有 `zigbee_demo` 入网，须**补齐/核对** OnOff Server） |

---

## 3. 子设备适配需求（Must / Should）

### 3.1 组网（Must）

1. 角色：**End Device**（允许 Router；禁止 Coordinator）。  
2. 在网关 `znp join on` 窗口内完成 Network Steering / 入网。  
3. 可 **leave / 清网** 后再次入网（按键或命令）。  
4. 串口或 LED 指示：搜网中 / 已加入 / 失败；打印 **IEEE** 与 **NWK**（与网关 `[ZNP] join` 对照）。  
5. 掉电后尽量保持网络信息（量产向；联调至少 leave 后能再入）。

### 3.2 ZCL 控灯契约（Must · 上市空中协议）

子设备**只认空中 ZCL**，不认网关 USART3 文本。

| 项 | 约定值 | 说明 |
|----|--------|------|
| Endpoint | **1** | 与网关默认 `dst_ep=1` 一致；若必须改 EP，须书面同步网关改默认 |
| Cluster | **OnOff `0x0006`** | 标准 Cluster，勿自创私有簇做本期验收 |
| 方向 | Server | 接收 Client→Server 命令 |
| Off | 命令 ID **`0x00`** | 关灯 / LED 灭 |
| On | 命令 ID **`0x01`** | 开灯 / LED 亮 |
| Toggle | 命令 ID **`0x02`** | 翻转 |
| 本地输出 | GPIO 灯或板载 LED | 人眼可见即可 |
| 日志 | 建议 `light: on` / `light: off`（或等价） | 与网关日志对拍 |

网关发送路径（供对照，子设备无需实现）：

```text
USART3: znp light on|off|toggle [nwk] [ep]
  → znp_host_zcl_onoff()
  → ZNP AF_DATA_REQUEST
  → 空中 ZCL：Frame Control 0x01 + seq + cmd
  → 你的 EP1 OnOff Server
```

### 3.3 信道（Must 对齐 · 量产说明）

| 阶段 | 要求 |
|------|------|
| **当前联调** | 多信道扫描已落地：`CONFIG_ZIGBEE_CHANNEL_MASK=0x02108800`（**11/15/20/25**），对齐网关 `ZNP_CHANNEL_MASK_DEFAULT` |
| **量产方向** | 掩码可按地区/SKU 调整；入网后跟协调器所在信道并写入 NV |
| **注意** | 相邻信道**不能**互连；必须与网关**同一信道编号**；换信道后需 leave 再 steer |

### 3.4 工程约束（Must）

| 项 | 要求 |
|----|------|
| SDK | 支持 nRF54L15 的 Zigbee R23 add-on；版本与 NCS 对齐以你们仓库为准 |
| 工程 | 独立 Zigbee 工程；**不要破坏**现有 BLE 量产工程 |
| BLE 共存 | 本期不要求同镜像双栈 |
| 交付物 | 可编译工程 + README：板型、烧录、入网键、leave 键、信道配置位置、OnOff EP 说明 |

### 3.5 明确不做（本期）

- 解析网关 `znp light …` ASCII  
- MQTT / 以太网  
- Level Control、Identify（Nice，非验收）  
- 与 `eink_tag` BLE 强行合一  

---

## 4. 网关侧操作（ZigBridge-EM · 已具备）

USART3 @ **115200**。

### 4.1 入网

```text
znp status                 # 期望 coord=1 st=9
znp join on 180
znp join status            # pj>0
# … 子设备触发入网 …
# [ZNP] join nwk=0xXXXX ieee=AABB…
znp join off
```

### 4.2 控灯（入网成功后）

```text
znp light on               # 默认：最近一次 join，ep=1
znp light off
znp light toggle
znp light on 0xXXXX        # 指定短地址
znp light on 0xXXXX 1      # 指定短地址 + EP
```

期望网关回包示例：`ok znp light_on nwk=0xXXXX ep=1`  
网关日志可能含：`[ZNP] onoff cmd=…`、`[ZNP] af_cnf …`

实现位置：`Common/Src/znp_host.c`（`znp_host_zcl_onoff`）、`Common/Src/cmd.c`（CLI）。

---

## 5. 建议联调步骤（双方）

| # | 负责方 | 动作 | 期望 |
|---|--------|------|------|
| 1 | 网关 | E72 为 coordinator；`znp status` → COORD | `coord=1 st=9` |
| 2 | 子设备 | 烧录 ED+OnOff 固件；信道含 11；必要时 leave 清旧网 | 就绪 |
| 3 | 网关 | `znp join on 180` | `pj>0` |
| 4 | 子设备 | 立即搜网/入网 | 本机 `joined` + IEEE/NWK |
| 5 | 双方 | 对照 IEEE | 与 `[ZNP] join … ieee=` **一致** |
| 6 | 网关 | `znp join off` | 关窗 |
| 7 | 网关 | `znp light on` → `off` → `toggle` | 子设备灯态变化 + `light: …` 日志 |
| 8 | 网关 | `znp ping` | 回归仍成功 |
| 9 | 双方 | （建议）子设备 leave → 再入网 → 再控灯 | A4 + 控灯仍可用 |

**失败优先查**：permit join 是否过期；信道是否同号；子设备是否仍绑旧 PAN；EP 是否为 1；是否实现的是 OnOff **Server** 而非仅 Client；距离/天线。

---

## 6. 验收标准（本期 · 转交清单）

| ID | 标准 | 通过 |
|----|------|------|
| A1 | nRF 以 ED（或约定 Router）跑 Zigbee | ✅ 软件（`zigbee_demo` ED） |
| A2 | `znp join on` 窗口内入网 | ✅ 实机对拍 |
| A3 | 网关 `[ZNP] join nwk=… ieee=…` 与子设备 IEEE **一致** | ✅ 实机对拍 |
| A4 | leave / 清网后再入网 ≥ 1 次 | ✅ 实机对拍（BTN2 双击） |
| A5 | 不影响双方主线工程（网关 ZNP；nRF BLE 量产路径） | ✅ 独立工程 |
| **A6** | `znp light on` → 灯/LED **亮**；串口有 on 类日志 | ✅ 软件 + 现场复核 |
| **A7** | `znp light off` → **灭**；`toggle` 可翻转 | ✅ 软件 + 现场复核 |
| A8 | （建议）掉电上电后仍在网且仍可 `znp light`（短地址以实际为准） | ☐ 建议补测 |
| 多信道 | 掩码 11/15/20/25 可跟上网关轮换信道 | ✅ 软件；建议换 ch 再验 |

A1–A5 为入网基线；**A6–A7 为本期控灯必过**。

---

## 7. 工作量提示（排期）

| 事项 | 估计 | 说明 |
|------|------|------|
| 仅入网（已有 demo 时） | 小 | 对拍 IEEE |
| 补 OnOff Server EP1 + 灯驱 + 日志 | 小～中 | **本期核心增量** |
| 多信道扫描 / NV 保持 | 小（已做四信道掩码） | 后续按 SKU 扩掩码 / 打印工作 ch |
| 塞进 `eink_tag` + BLE 共存 | 大 | **本期不做** |

---

## 8. 参考路径

| 路径 | 说明 |
|------|------|
| [`product_full_process_guide.md`](product_full_process_guide.md) | 量产全流程；§5-F 控灯契约 |
| [`znp_net_acceptance.md`](znp_net_acceptance.md) | 组网 / permit join |
| [`znp_ping_acceptance.md`](znp_ping_acceptance.md) | ZNP 冒烟 |
| [`zigbee_demo_handoff_to_zigbridge.md`](zigbee_demo_handoff_to_zigbridge.md) | 入网联调交接（ZCL 以本文为准） |
| `Common/Src/znp_host.c` / `znp_host.h` | 网关 ZCL 发送 API |
| `TY/nRF54L15/README.md` | 子设备仓入口 |

**请求方**：ZigBridge-EM（需要可入网且可被 OnOff 控制的标准子设备）。  
**承接方**：nRF54L15 固件（ED + OnOff Server 镜像与 README）。

---

## 9. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-10-13 | 初稿：入网联调；信道 11；ZCL 为非目标 |
| 2026-10-30 | **转交更新**：纳入量产背景；Must 增加 EP1/OnOff `0x0006`；网关 `znp light` 已就绪；验收增 A6/A7；明确子设备不解析 CLI 文本 |
| 2026-10-30 | nRF 交付：`zigbee_demo` 补 EP1 OnOff Server + 交付文 [`zigbee_demo_handoff_to_zigbridge.md`](zigbee_demo_handoff_to_zigbridge.md)；A1/A5/A6–A7 软件就绪，实机对拍待同场 |
| 2026-10-31 | 多信道扫描 11/15/20/25；显式配网窗/leave；A2–A4 实机对拍；见 [`zigbee_demo_pairing_control.md`](zigbee_demo_pairing_control.md) |
