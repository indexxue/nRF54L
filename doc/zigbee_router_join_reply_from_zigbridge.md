# 回传：ZigBridge 协调器侧会诊 — nRF `zigbee_router` 入网后 ~10s leave

> **给谁看**：nRF54L15 `zigbee_router` 子设备同事  
> **对照来文**：[`zigbee_router_join_issue_to_zigbridge.md`](zigbee_router_join_issue_to_zigbridge.md)  
> **网关工程**：`TY/ZigBridge-EM`（H743 主机 + E72 Koenkk ZNP Coordinator）  
> **日期**：2026-11-01  
> **结论摘要**：**H743 主机固件暂不改 TCLK/入网策略代码**；现象符合「关联成功、TCLK/配网收尾未完成」；根因优先查 **E72 ZNP 栈与 nRF Router 空中密钥交换**，主机可补诊断日志（可选，不阻塞子设备继续排查）。

---

## 1. 一句话判断

| 问题 | 协调器侧判断 |
|------|----------------|
| Router 能否被 permit join 看见？ | **能**。`join(tc)` + IEEE 对拍已证明关联路径通。 |
| `join(tc)` = 可长期在网 / TCLK 完成？ | **否**。主机只记 nwk+IEEE，**不表示** Transport Key / TCLK 成功。 |
| 主机是否拦截 Router / 只允许 ED？ | **否**。无按角色过滤；PJ 还会广播 `0xFFFF` 让已有 Router 开窗。 |
| 主机是否缺「手写 TCLK/APSME」导致 leave？ | **否（相对 Z2M/herdsman 模型）**。TCLK 由 **E72 内 Z-Stack 3 协调器固件**完成；H743 不发 Transport Key。 |
| 与 ED 对照 | 同网关、同契约 ED 可入网+控灯 → 更像 **Router 角色 / TCLK 收尾** 差异，而非 EP/Cluster 契约错误。 |
| **当前是否必须改 H743 固件才能继续测？** | **不必**。建议先空中抓包 / 升级或核对 E72 hex / Router 侧安全 trace；主机侧诊断增强为**可选**。 |

---

## 2. 逐条答复来文 §6

### 2.1 ZNP 是否完整支持 Router 的 TCLK？

- **设计模型**：本仓与 Zigbee2MQTT / herdsman 同类——主机做 **建网 + permit join + 记设备 + AF/ZCL**；**Trust Center 密钥交换在协调器 ZNP（E72）内自动完成**。  
- H743 `znp_host` **没有**、也**不需要**先实现 APSME Transport Key / Request Key，才能让标准 Z3 设备入网（ED 已证明主路径可用）。  
- 因此：**不能**从「主机无 TCLK 代码」直接推出「协调器不支持 Router TCLK」。若空中确实无 Transport Key / Request Key，优先怀疑：  
  1. **E72 hex 行为 / 版本**（见 §4）  
  2. **nRF Router / ZBOSS 侧发起或收钥**  
  3. 射频/时序导致密钥帧丢失（相对少见，但 sniffer 可一眼区分）

### 2.2 `join(tc)` 含义（重要）

| 日志 | 主机真实含义 |
|------|----------------|
| `[ZNP] join(tc) nwk=… ieee=…` | 收到 `ZDO_TC_DEV_IND (0x45CA)`：TC 侧看到设备，写入 RAM `s_last_join` |
| `[ZNP] join(annce) …` | 收到 `ZDO_END_DEVICE_ANNCE_IND (0x45C1)`（Router/ED 都可能发；本仓未解析 Capability 位） |

**不是**：

- TCLK 完成标志  
- Steering 成功标志  
- 「可以量产控灯」标志（控灯另需设备仍在网 + ZCL OnOff）

故来文时间线「`join(tc)` 后约 10s leave + `STEERING st=-1`」与主机语义**一致**：半入网 / 关联成功，**配网收尾失败**。子设备以 **`sig=10 st=0` 才算真正入网** 的判据正确，请继续坚持。

### 2.3 permit join 对 Router 有无特殊限制？

主机侧：

- `znp join on` → `ZDO_MGMT_PERMIT_JOIN_REQ`：先协调器本地（`0x0000` / 失败再 `0xFFFC`），成功后再广播 `0xFFFF`  
- **无**「仅 ED」策略、**无**安装码强制、**无**按 `device_type` 拒收  
- 未改 Z-Stack 默认集中式安全以外的主机策略项（无 `PRECFGKEY` / install-code API 调用）

若存在限制，更可能在 **E72 内置栈默认策略**，而不是 H743 应用逻辑。

### 2.4 协调器完整日志 / leave 可见性（能力边界）

当前主机 AREQ 具名处理仅有：

- `0x45C0` state  
- `0x45C1` annce → `join(annce)`  
- `0x45CA` tc → `join(tc)`  
- `0x4480` af_cnf  

**未具名处理** `ZDO_LEAVE_IND`（常见 `0x45C9`）及密钥类指示；其它 `0x45xx` 仅有泛化 `zdo areq cmd1=…` 日志。  
因此：

- 同场若只有 `join(tc)`、约 10s 后无具名 leave 日志，**不能**据此否定设备侧 leave——主机可能根本没解析 leave。  
- 设备 leave 后 `s_last_join` **仍可能有效** → `znp light` 仍会往旧 nwk 发，易误判为「控灯路径坏了」而非「已不在网」。

**请子设备侧**：继续以本机 `sig=3 LEAVE` / `STEERING st=-1` 为准；需要网关同场 USART3 时，请贴含 `znp join on` → `join(tc)` → 随后 15s 内全部 `[ZNP]` 行。

### 2.5 ED ↔ Router 对照实验

同意来文建议，且网关侧认定：

- **ED 成功 + Router 稳定复现 leave** → 标为 **「Router 入网收尾 / TCLK 路径」联调问题**，不是 OnOff 契约问题。  
- 主机 **暂不**因此合入「手写 TCLK」大改；优先空中证据 + E72 hex 核对。

---

## 3. 固件要不要改？（给双方排期）

### 3.1 H743 `znp_host` / `cmd` — 结论

| 类别 | 是否现在改 | 说明 |
|------|------------|------|
| 实现主机侧 Transport Key / Request Key | **否** | 与现架构不符；ED 已通说明主机模型可用 |
| 为 Router 单独开/关 permit 或角色策略 | **否** | 当前无此类限制可关 |
| 补 leave / Capability / 未知 ZDO 诊断日志 | **可选（建议）** | 便于下次同场对拍；**不修复**空中密钥失败本身 |
| 设备表 M6 / 持久化多设备 | **否（本期）** | 与本次 leave 根因无关 |

**排期建议**：子设备 / sniffer / E72 hex 排查**并行**；诊断日志等联调需要时再合入，不作为 Router 侧改码的前置条件。

### 3.2 E72 ZNP hex — 建议核对（网关硬件侧）

| 项 | 现状 |
|----|------|
| 模组 | E72-2G4M20S1E（CC2652P） |
| 镜像族 | Koenkk **`CC1352P2_CC2652P_other_coordinator`**（勿用 launchpad） |
| 已知验收包 | `…_other_coordinator_20230507`，`SYS_PING cap=0x0659` |
| 说明 | 见 [`../tools/e72_znp_flash/README.md`](../tools/e72_znp_flash/README.md) |

若 sniffer 显示 **无 Transport Key / Request Key**，网关侧下一步优先：**换用更新的同系列 `other_coordinator` hex 复测**（仍须 `other`），再决定是否提 Koenkk/Z-Stack issue。

---

## 4. 建议子设备下一步

1. **可恢复改码 / 加调试**（不必死等主机大改）：  
   - APS / 安全 / TCLK 相关 ZBOSS trace（Request Key、Transport Key 收发、失败原因码）  
   - 确认 Router 未在 TCLK 完成前 `rejoin_abort` / 关 commissioning（来文已改，请保持）  
2. **空中抓包（强烈建议，一轮即可定性）**：关注 association 之后是否出现  
   - Update Device  
   - Transport Key  
   - Request Key  
   - Device Annce  
   以及 leave 原因  
3. **对照**：同板同信道只换 `zigbee_ed` —— 预期仍可控灯；再换 `zigbee_router` —— 预期复现 leave。  
4. **脏 NV**：BTN2 长按擦 `zboss_nvram` 后复测仍然有效；若曾用 ED 与 Router 交替刷机，每次换角色后务必擦净。  
5. **控灯验证时机**：仅在出现 `sig=10 st=0` 后再让网关 `znp light`；此前失败**不**计为 OnOff 回归失败。

---

## 5. 同场复测操作单（双方）

```text
[网关] 确认 COORD：znp info / LCD
[网关] znp channel          # 记下 ch∈{11,15,20,25}
[网关] znp join on 180
[Router] 已擦 NV（如刚换角色）→ BTN1 steering
[网关] 期望：约数秒内 [ZNP] join(tc) … ieee=…（端序与子设备互逆属正常）
[双方] 盯住随后 ~15s：
       Router：是否 sig=3 leave、sig=10 st=-1，有无任何 key 相关 trace
       网关：有无其它 [ZNP] zdo areq / leave；勿因 last_join 仍在而强行认定「已在网」
[仅当 Router STEERING st=0] 再 znp light on/off
[网关] znp join off
```

IEEE 对拍规则不变：协调器打印与子设备打印为同一 EUI64 的**字节序互逆**时视为同一设备（来文 §3 已正确）。

---

## 6. 回传清单（对应来文 §9）

| # | 项 | 内容 |
|---|----|------|
| 1 | 对「Router join(tc) 后 ~10s leave」的判断 | **高度符合 TCLK/配网收尾失败时序**；`join(tc)`≠TCLK 完成。主机无 Router 禁入策略。根因不优先归为「H743 缺手写 TCLK」。 |
| 2 | 同场协调器原始日志 | 请 Router 侧下次复现时同步抓 USART3；本回传基于代码审计 + 来文日志。若需要，网关可另开「诊断日志」小改后再同场抓一版。 |
| 3 | 建议子设备下一步 | **可继续排查**：安全/APS trace + **sniffer**；网关并行核对/试升级 E72 `other_coordinator`。**暂缓**等待「主机实现 TCLK」；**无需**为等主机而停一切改码。 |

---

## 7. 责任切分（下一里程碑）

| 侧 | 当前动作 |
|----|----------|
| **Router 子设备** | 开 TCLK/安全 trace；有条件上 sniffer；保持「仅 STEERING OK 才 joined」 |
| **ZigBridge H743** | 默认不改入网/TCLK 逻辑；按需加 leave/Capability 诊断（另排） |
| **ZigBridge E72** | 确认 hex 为 `other_coordinator`；必要时升版本复测 Router |
| **双方** | 契约不变：EP1 · OnOff `0x0006`；掩码 `0x02108800`；角色允许 Router |

---

## 8. 相关文档

| 文档 | 说明 |
|------|------|
| [`zigbee_router_join_issue_to_zigbridge.md`](zigbee_router_join_issue_to_zigbridge.md) | 来文（问题单） |
| [`product_full_process_guide.md`](product_full_process_guide.md) | 全流程；Router 本期未强制验收 |
| [`znp_net_acceptance.md`](znp_net_acceptance.md) | M5 入网验收（ED） |
| [`gateway_control_logic.md`](gateway_control_logic.md) | `join(tc/annce)` / permit join 逻辑 |
| [`../tools/e72_znp_flash/README.md`](../tools/e72_znp_flash/README.md) | E72 Koenkk hex 选型 |

**网关承接**：ZigBridge-EM（会诊结论如上；诊断小改可选）  
**子设备承接**：nRF54L15 `zigbee_router`（建议恢复安全侧调试 + sniffer）
