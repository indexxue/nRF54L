# 需求说明：nRF54L15 作为 Zigbee 子设备联调 ZigBridge-EM

> **文档用途**：跨仓库交接 / 需求传递（ZigBridge-EM ↔ nRF54L15）。  
> **结论摘要**：**可以**。nRF54L15 官方支持 Zigbee End Device（及 Router）；可作为本网关协调器的联调子设备。  
> **现状摘要**：ZigBridge-EM 侧协调器 + permit join 已就绪；nRF54L15 仓库当前以 **BLE** 为主，**尚未集成 Zigbee 协议栈**，需新增工程或样例移植。

---

## 1. 背景与目标

| 项 | 内容 |
|----|------|
| 网关工程 | `TY/ZigBridge-EM`（STM32H743 + E72/CC2652P ZNP） |
| 子设备候选工程 | `TY/nRF54L15`（与 ZigBridge-EM **同级**） |
| 网关已完成 | ZNP ping、协调器建网（`state=9`）、`znp join on` 开门 |
| 本需求目标 | 用 nRF54L15 板作 **Zigbee 子设备（优先 End Device）**，成功加入网关网络，并在网关 USART3 日志中看到入网成功指示 |
| 非目标（本期） | ZCL 业务互操作、MQTT 上报、设备表持久化、量产证书、BLE/Zigbee 长期共存产品化 |

验收对齐：[`znp_net_acceptance.md`](znp_net_acceptance.md) 步骤 #5（可选子设备入网）。

---

## 2. 可行性结论

### 2.1 芯片 / SDK 能力（官方）

Nordic **nRF54L15** 支持 IEEE 802.15.4 上的 Zigbee。在 *Matter and Zigbee add-on for nRF Connect SDK* 成熟度表中，nRF54L15 标注：

| 能力 | 状态 |
|------|------|
| Zigbee | Supported |
| Zigbee (Sleepy) End Device | Supported |
| Zigbee Router | Supported |
| Zigbee FOTA | Supported |

官方样例（Zigbee R23 add-on）含：

- **Zigbee: Light switch** → End Device（推荐作联调子设备）
- **Zigbee: Light bulb** → Router
- **Zigbee: Network coordinator** → **不要**用于本联调（协调器角色由 ZigBridge-EM / E72 承担）

参考：

- [Software maturity（Zigbee on nRF54L15）](https://nrfconnectdocs.nordicsemi.com/addons/ncs-matter-zigbee/latest/software-maturity.html)
- [Zigbee Light switch sample](https://nrfconnectdocs.nordicsemi.com/addons/ncs-zigbee/latest/samples/light_switch/README.html)
- [nRF54L15 Zigbee CID 兼容矩阵](https://docs.nordicsemi.com/r/bundle/comp_matrix_nrf54l15/page/comp/nrf54l15/nrf54l15_zigbee_cids.html)

### 2.2 与网关协议栈的互操作预期

| 侧 | 栈 / 角色 |
|----|-----------|
| ZigBridge-EM | Host：H743 `znp_host`；射频：E72 烧 **Koenkk Z-Stack 3.x coordinator**（已测 `cap=0x0659`） |
| nRF54L15 | Nordic **ZBOSS / Zigbee R23 add-on**，角色 **End Device**（或 Router） |

Zigbee 3.0 标准入网（Network Steering / permit join）跨厂商协调器与终端一般可互通。  
**风险点**（联调时优先排查）：信道不一致、子设备已绑旧网、Trust Center / install code 策略、双方固件版本过旧。本期联调以「能 association + End Device Announce」为准，不要求完整 HA 集群业务。

### 2.3 当前仓库现状（务必读）

| 仓库 | 现状 | 含义 |
|------|------|------|
| ZigBridge-EM | `znp start` / `znp join on` 已验收；入网成功打日志 `[ZNP] join nwk=… ieee=…` | **网关侧基本不用再开发**即可验子设备入网 |
| nRF54L15 | NCS **3.2.x**；现有 `demo` / `eink_tag` / `factory` 走 **BLE**；无 Zigbee 模块 | **不能**直接用现有 BLE 镜像当子设备；需新增 Zigbee ED 工程或官方 sample 移植 |

---

## 3. 需求（给 nRF54L15 侧）

### 3.1 功能需求（Must）

1. **角色**：Zigbee **End Device**（优先）；允许 Router，但禁止再做 Coordinator。  
2. **组网行为**：上电或按键触发后，在协调器 **permit join 窗口打开期间** 发起搜网 / 入网（Network Steering）。  
3. **信道**：默认固定扫描或加入 **Channel 11**（与网关 `bdbSetChannel(ch11)` 一致）。若实现多信道扫描，须 **包含 11**。  
4. **退网 / 重入网**：提供一键或串口命令 **恢复出厂 / leave network**，便于反复联调。  
5. **可观测**：串口（或 LED）明确指示：搜网中 / 已加入 / 失败。记录并打印本机 **IEEE / NWK 短地址**（便于与网关日志对照）。  
6. **硬件**：使用现有 nRF54L15 开发板或自有板均可；天线 / 2.4 GHz 通路正常即可。

### 3.2 非功能 / 工程约束

| 项 | 要求 |
|----|------|
| SDK | 使用支持 nRF54L15 Zigbee 的 **Zigbee R23 add-on**（与当前 NCS 3.2.x 的兼容版本需在实现时核对；若 add-on 仍钉在 NCS 2.9.x，允许单独建立 Zigbee 联调工程与工具链，不必强行塞进现有 `eink_tag`） |
| 工程形态 | 建议新建 `projects/zigbee_ed`（或同等命名），**不要破坏**现有 BLE 量产路径 |
| 与 BLE 共存 | 本期 **不要求** BLE+Zigbee 同时工作；联调镜像可纯 Zigbee |
| 交付物 | 可编译工程 + 简短 README（构建板型、烧录、入网按键/命令、信道配置位置） |

### 3.3 可选（Nice）

- Sleepy End Device（低功耗）— 联调成功后再做。  
- 简单 OnOff / Identify 集群 — 便于后续 ZCL 阶段，**非本期验收条件**。  
- 固定 install code 预置 — 仅当网关侧后续启用严格 TC 策略时再对齐。

---

## 4. 网关侧配合（ZigBridge-EM，已具备）

USART3 @ 115200，协调器已 `state=9` 时：

```text
znp status              # 期望 coord=1 st=9
znp join on 180         # 开 180s 入网窗
znp join status         # pj>0
# … 此时让 nRF54L15 入网 …
# 成功日志示例：
# [ZNP] join nwk=0xXXXX ieee=AABBCCDDEEFF0011
znp join off
znp ping                # 回归
```

入网成功判定（网关）：

- 日志出现 `[ZNP] join nwk=… ieee=…`（来自 `ZDO_END_DEVICE_ANNCE_IND`）  
- （辅助）`znp info` 中 `assoc` 可能增加  

协调器侧实现位置：`Common/Src/znp_host.c`（已实现，无需为联调再改）。

---

## 5. 建议联调步骤（双方）

| # | 负责方 | 动作 |
|---|--------|------|
| 1 | ZigBridge-EM | 确认 E72 为 coordinator hex；`znp start` → `coord_up` |
| 2 | nRF54L15 | 烧录 Zigbee ED 固件；确认信道含 **11**；必要时擦除旧网络信息 |
| 3 | ZigBridge-EM | `znp join on 180` |
| 4 | nRF54L15 | 立即触发入网 / 上电搜网 |
| 5 | 双方 | 网关见 `[ZNP] join …`；子设备见「已加入」+ 打印 IEEE/NWK |
| 6 | ZigBridge-EM | `znp join off`；`znp ping` 回归 |

失败时优先检查：信道、permit join 是否仍打开、子设备是否仍绑定旧 PAN、两板距离 / 天线。

---

## 6. 验收标准（本期）

| ID | 标准 | 通过 |
|----|------|------|
| A1 | nRF54L15 以 ED（或 Router）角色运行 Zigbee 固件 | ☐ |
| A2 | 网关 `znp join on` 窗口内完成入网 | ☐ |
| A3 | 网关 USART3 出现 `[ZNP] join nwk=… ieee=…`，且 IEEE 与子设备打印一致 | ☐ |
| A4 | 子设备可 leave / 恢复出厂后再次入网至少 1 次 | ☐ |
| A5 | 不影响双方既有主线（ZigBridge ZNP；nRF54 BLE 工程） | ☐ |

---

## 7. 工作量与边界（供排期）

| 事项 | 估计 | 说明 |
|------|------|------|
| 官方 light_switch → 板级跑通 | 小～中 | 最快路径；先验证射频与入网 |
| 纳入 `TY/nRF54L15` 仓库工程化 | 中 | 新建 project、文档、与 NCS/add-on 版本对齐 |
| 塞进 `eink_tag` 与 BLE 共存 | 大 | **本期不做** |
| 网关设备表 / ZCL / MQTT | — | ZigBridge 后续阶段，不在本文 |

---

## 8. 联系与参考路径

| 路径 | 说明 |
|------|------|
| `TY/ZigBridge-EM/doc/znp_net_acceptance.md` | 网关组网 + permit join 验收 |
| `TY/ZigBridge-EM/doc/znp_ping_acceptance.md` | ZNP 冒烟前置 |
| `TY/ZigBridge-EM/Common/Src/znp_host.c` | 入网日志实现 |
| `TY/nRF54L15/README.md` | 子设备侧仓库入口（当前 BLE） |

**请求方**：ZigBridge-EM（网关联调需要一台可控的标准 Zigbee 子设备）。  
**承接方**：nRF54L15 固件（提供 ED 联调镜像与操作说明）。

---

## 9. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-10-13 | 初稿：确认芯片可行；明确网关已就绪、nRF 需新增 Zigbee ED；信道 11；验收看网关 join 日志 |
