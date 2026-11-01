# 再关联约定：网关 ↔ ED / Router

> **文件名**：`doc/zigbee_rejoin_contract.md`  
> **文档性质**：**共同契约**，同时约束 **End Device 与 Router**。联调 / 量产排障时，以本文对「掉电后能否再控 / 再入网」的理解与责任划分为准。  
> **关联**：空中 ZCL 以 [`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md) 为准；交付步骤见 [`zigbee_demo_handoff_to_zigbridge.md`](zigbee_demo_handoff_to_zigbridge.md)；配网状态机见 [`zigbee_demo_pairing_control.md`](zigbee_demo_pairing_control.md)；设备表验收见网关仓 `device_table_acceptance.md`。  
> **状态日期**：2026-11-01  
> **评审**：§1–§4、§5（工位）、§6（网关实现方向）、**§2.3 rejoin 分界** — **网关侧已同意（2026-11-01）**；**先改文档，后改代码**。  
> **已确认现场事实（本仓工位）**：协调器**整机掉电**后 `ch/pan` **同号**，但**控灯失败**；Router **清 NV 后可重新加入**。
>
> ### 双仓同步（改本文时必须同时做）
>
> | 项 | 内容 |
> |----|------|
> | **成对路径** | `TY/ZigBridge-EM/doc/zigbee_rejoin_contract.md` ↔ `TY/nRF54L15/doc/zigbee_rejoin_contract.md` |
> | **规则** | **同名同路径、正文语义一致**；修订须**两边同一轮改完**，禁止只改一仓就当定稿 |
> | **操作** | 改完本仓后立刻把**同一 diff 语义**写入对端；§9 修订记录两边各记一行；签字栏/日志关键字等联调字段两边对齐 |
> | **允许差异** | 仅「关联」链接可按本仓可达路径微调（如本仓相对链接 vs 对端「见 nRF 仓 …」）；**§0–§6 契约正文、§2.3 日志关键字、签字结论不得分叉** |
> | **Agent** | 编辑本文件时**同回合**同步对端；未同步不得声称契约已更新 |

---

## 0. 产品目标与一句话共识

### 0.1 产品 Must（双向找回 · 2026-11-01 确认）

| # | 场景 | 期望 |
|---|------|------|
| P1 | **网关重启**（同网 Path A，未 clear） | 能**自动找回**仍在同网的子设备：刷新 nwk、标 online，之后可控灯 |
| P2 | **子设备重启**（本地仍有网身份） | 能**自动回到**原网关同网：同网 rejoin / 父节点恢复，直至应用可达 |

二者缺一不可：只靠设备表「记得 IEEE」不算找回；只靠子设备本地显示 `joined` 也不算回到网关。

### 0.2 技术共识

**同一张 Zigbee 网的「关联锚点」是两边协议栈 NV 里的网络身份（信道 / ExtPAN / 密钥等），不是网关 H743 上的设备表。**  
整机掉电后若网身份仍在：子设备 Must **同网 rejoin**（§2.3）；网关上电先 **stale**，再靠 **join/annce 和/或主动地址探测** 找回（§6）。

---

## 1. 三本账（禁止混为一谈）✅ 已同意

| 层 | 存放位置 | 内容 | 掉电后角色 |
|----|----------|------|------------|
| **A. 网络身份（真锚点）** | E72 NV ↔ 子设备 NV | 信道、PAN/ExtPAN、NWK key、本机短地址等 | **同则应能回到同一张网**；不同则已「断代」 |
| **B. 栈关联（assoc）** | E72 / 子设备协议栈内部 | 父子、邻居、assoc 表 | 可能与 A 不同步；诊断用（如 `znp info` 的 assoc） |
| **C. 设备表（应用通讯录）** | 网关 H743（M6：外扩 NOR） | IEEE ↔ 缓存 nwk ↔ ep ↔ 类型 | **仅供点名控灯 / 日后 MQTT**；**不能**替代 A，也**不能**强迫 Router 入网 |

```text
控灯成功的前提 = A 同网 + 子设备已对协调器可达（rejoin/assoc 有效）+ 网关用对短地址发 ZCL
仅有 C（设备表）或仅有「曾经 join 过」≠ 当前可控
```

---

## 2. 两种掉电结局（必须先分清）✅ 已同意

### 2.1 同网恢复（本次已确认的主路径）

| 观察 | 含义 |
|------|------|
| 整机掉电（非 `znp clear`） | 协调器侧走 Path A，**不**故意建新网 |
| 掉电后 `znp channel` / pan **与掉电前同号** | **网络身份 A 未断代** |
| 同号但控灯失败 | **可达性 / 关联新鲜度问题**，不是「换了一张网」 |
| Router 清 NV 后能再 join | 子设备掉电后仍持有旧网记忆，**未完成对协调器的有效 rejoin**；清 NV 等于强制重新 association |

**双方责任（同网恢复）**：

| 侧 | Must |
|----|------|
| **子设备（ED / Router）** | 见 **§2.3**：已入网身份仍在时 Must **同网 rejoin**；禁止假在网 |
| **网关** | Path A 恢复同网后：设备表条目视为 **stale/offline**，直至 `TC_DEV_IND` / `ANNCE` /（约定的）可达确认；不得仅因 NOR 里有 IEEE 就默认 online 控灯 |
| **双方** | 同网恢复**不要求**用户先清子设备；清子设备 NV 是**排障手段**，不是量产正常路径 |

### 2.2 断代（清网 / 新 form）

| 触发 | 结果 |
|------|------|
| 网关 `znp clear` / 长按清 NV / E72 网络身份丢失后新建网 | 新信道或新 PAN → **A 断代** |
| 子设备仍保留旧网 NV | 会去找旧网；对新网表现为「加不进去」，直到子设备 leave/清 NV |

**双方责任（断代）**：

| 侧 | Must |
|----|------|
| **网关** | `znp clear` 时清空设备表；文档与 UI 标明「子设备须 leave/清 NV 后才能加入新网」 |
| **子设备** | 提供可靠 **leave / 清 NV**（按键或命令）；清网后可在 `znp join on` 窗口内重新 steering |
| **产线/测试** | 清协调器网与清子设备必须成对理解，禁止只清一侧还期望立刻互通 |

### 2.3 rejoin 分界（Must · 已同意）

把「自动搜新网」和「同网再附着」拆开，避免与「显式入网」原则打架：

| 子设备本地状态 | 允许 / 要求 | 禁止 |
|----------------|-------------|------|
| **未入网**（无合法网身份：出厂、leave 后、清 NV 后） | 仅 **显式** Network Steering（如 `steer_request` / 入网键） | **禁止**上电自动 steering；**禁止**把 Nordic 默认「未入网自动搜网」当正常路径 |
| **已入网身份仍在**（NV 仍有本网；含本机重启、协调器整机掉电后同网恢复、父节点丢失） | **Must** 对**同一张网**做 **rejoin / 父节点恢复**（或栈等价），直到能收发应用数据（含 ZCL） | **禁止**长期「JOINED/自以为在网」却不可达且不发起同网 rejoin；**禁止**在此场景用 `rejoin_abort` 掐掉同网恢复 |

```text
未入网  →  不自动搜网；等人按键 steering（找新网 / 初次入网）
已入网  →  必须能同网 rejoin（协调器重启后仍回到同一小区）
断代后  →  先 leave/清 NV（变未入网），再显式 steering
```

**日志约定（子设备 `zigbee_ed` / `zigbee_router`，与网关 `join(tc|annce)` 对照）**：

```text
zigbee: rejoin start reason=parent_unreachable|parent_link_fail|reboot_fail|reboot_joined|…
zigbee: rejoining (same-net)
zigbee: rejoin retry delay_ms=N
zigbee: rejoin fail st=N (will retry)
zigbee: rejoin ok nwk=0x.... ieee=...
zigbee: joined nwk=0x.... …          # 恢复成功后仍会跟既有 joined 行
```

触发：`NO_ACTIVE_LINKS` → `parent_unreachable`；`NLME_STATUS` 父链路失败 → `parent_link_fail`（ED 挂 Router 关掉后常见）；`DEVICE_REBOOT` → `reboot_joined` / `reboot_fail`。未入网路径仍 abort Nordic 自动搜网。

---

## 3. 角色与能力边界 ✅ 已同意

### 3.1 网关（ZigBridge-EM）

| 做 | 不做 |
|----|------|
| E72 协调器建网 / Path A 恢复 / Permit Join | 替子设备擦除其子设备片上 Zigbee NV |
| 记录 join/leave → 设备表（IEEE 主键） | 把设备表当作射频关联锚点 |
| 按 IEEE / 表下标发 ZCL OnOff（EP1 / `0x0006`） | 在子设备未 rejoin 时保证控灯成功 |
| 上电后标 stale、用指示刷新 online（§6，先文档后代码） | 假设 Router 永不需要 rejoin |

### 3.2 子设备（ED 与 Router 均适用，除非另行注明）

| 做 | 不做 |
|----|------|
| 扫描掩码覆盖网关默认 **11/15/20/25** | 解析网关 USART3 ASCII（`znp light …`） |
| EP1 OnOff Server；掉电尽量保留网络身份 | 依赖网关设备表「拉自己入网」 |
| **§2.3 分界下的同网 rejoin** | 未入网时自动 steering；假在网不 rejoin |
| 可 leave / 清 NV 再入（排障与断代后） | 在断代后仍要求不经 leave 就加入新网 |

**Router 额外注意**：常供电、可转发；掉电/协调器重启后同样适用 §2.3。若只 ED 能 rejoin、Router 不能，视为**子设备侧缺陷**，不是网关设备表缺陷。

---

## 4. 联调判定表（双方对照同一张表）✅ 已同意

| # | 现象 | 先查 | 归责倾向 |
|---|------|------|----------|
| T1 | 掉电后 ch/pan **变了**（未打算 clear） | 网关是否误 form；E72 NV | 网关 |
| T2 | 掉电后 ch/pan **同号**，控灯失败 | 子设备是否同网 rejoin；网关是否假 online | **优先子设备 §2.3**；网关须避免假 online |
| T3 | 同号控失败，**清子设备 NV 后能 join 且可控** | 印证「未 rejoin / 假在网」 | 子设备补同网 rejoin；网关补 stale |
| T4 | `znp clear` 后子设备不加新网 | 子设备是否仍持旧 NV | 操作流程：清子设备；非信道契约错误 |
| T5 | 新 join 有日志但 light 无反应 | EP/Cluster；IEEE 是否指对设备 | 契约 / 选址 |

---

## 5. 推荐工位步骤（同网掉电复现）✅ 已同意

```text
[双方] 记录掉电前：znp channel、pan、znp devices（ieee/nwk）、子设备 ieee
[网关] 整机掉电再上电（勿 znp clear）
[网关] znp channel / status → 确认与掉电前同号、coord=1
[网关] znp devices → 约定：应视为 stale/offline，直至新指示
[子设备] 上电后：应走同网 rejoin（§2.3），日志有约定关键字
[网关] 见 join(tc|annce) 或约定可达证据后 → light iN / ieee 应可控
[负例] 若子设备无 rejoin 且一直假在网 → 允许 leave/清 NV 验证能再入；此负例不得标为「网关 M6 失败」
```

---

## 6. 网关侧实现承诺 ✅ 已同意 · **网关代码已合入（2026-11-01）**

落实 §0.1 P1 + 假 online 治理：

1. COORD 恢复前：`device_table_mark_all_offline` + 清 `last_join`；NOR load 强制 `online=false`。  
2. online 证据：`upsert`（TC/ANNCE）**或** COORD 后主动探测成功。  
3. `znp light`：`offline` / `no_dev`；表内 offline 的 nwk 旁路也拒绝。  
4. **主动找回（P1）**：`znp_recover_device_book_locked` — 对表内行先 `ZDO_IEEE_ADDR_REQ`（缓存 nwk），失败再 `ZDO_NWK_ADDR_REQ`（IEEE）；成功则 upsert online。自动在 `start_network` 成功后执行；也可 `znp recover`。

**子设备侧实现承诺**（对齐 §2.3，`zigbee_ed` / `zigbee_router` 的 `zigbee_app`）：

1. **未入网**：保持「禁止默认自动搜网」；仅显式 steering。 ✅  
2. **已入网身份仍在**：同网 rejoin（`NO_ACTIVE_LINKS` / reboot fail）；不 abort 掐死恢复。 ✅  
3. README / 串口关键字：见 §2.3 日志约定。 ✅

---

## 7. 与其它文档的关系

| 文档 | 管什么 |
|------|--------|
| 本文 | **掉电 / 同网再关联 / 断代 / rejoin 分界 / 三本账** |
| `req_nrf54_zigbee_end_device.md` | EP、Cluster、OnOff 命令、扫描掩码 |
| `zigbee_demo_handoff_to_zigbridge.md` | 交付清单与日常入网控灯步骤 |
| `zigbee_demo_pairing_control.md`（nRF） | 子设备状态机；须与 §2.3 一致 |
| `device_table_acceptance.md` | 多设备表与 NOR 持久化（应用层） |
| `znp_net_acceptance.md` | 建网 / 信道 / clear 轮换 |

冲突时：**空中帧格式以 req 为准；掉电再关联与 rejoin 分界以本文为准**；pairing 文档不得再写「已入网重启只标 JOINED、无需可达恢复」而无 rejoin。

---

## 8. 双方签字栏（联调确认）

| 项 | 网关侧 | 子设备侧 |
|----|--------|----------|
| 已读本文 §0–§4、§2.3 | ☑ 2026-11-01 | ☑ 2026-11-01 |
| 接受：同网掉电后子设备 Must rejoin（§2.3） | ☑ | ☑ |
| 接受：未入网禁止自动 steering | ☑ | ☑ |
| 接受：设备表 ≠ 关联锚点 | ☑ | ☑ |
| 接受：clear 断代后须清子设备再入 | ☑ | ☑ |
| 接受：§6 先文档后代码 | ☑ | ☑（子设备 §2.3 代码已合入） |
| 现场 T2/T3 复现结论一致 | ☐ 日期：____ | ☐ 日期：____ |
| 工程 / 提交号 | ZigBridge-EM：________ | 子设备：nRF54L15 `zigbee_ed`/`zigbee_router`（提交号联调后填） |

---

## 9. 修订记录

| 日期 | 说明 |
|------|------|
| 2026-11-01 | 初版：三本账；同网恢复 vs 断代；整机掉电同号控失败 + Router 清 NV 可再入 |
| 2026-11-01 | 更名 → **`zigbee_rejoin_contract.md`** |
| 2026-11-01 | 评审通过 §1–§6；新增 **§2.3 rejoin 分界**；网关签字栏勾选；明确先文档后代码 |
| 2026-11-01 | 网关 §6 落地：`mark_all_offline`、NOR stale load、`znp light`→`offline` |
| 2026-11-01 | 产品目标 §0.1 P1/P2；网关主动 `recover`（IEEE/NWK addr req） |
| 2026-11-01 | 子设备实现 §2.3：同网 rejoin + 日志关键字回填；子设备签字栏勾选；T2 现场仍待工位复测 |
| 2026-11-01 | 文首补强「双仓同步」操作表；与 ZigBridge-EM 仓同回合对齐正文 |
| 2026-11-01 | 补 `parent_link_fail`（NLME 父链路失败触发同网 rejoin；ED 挂 Router 场景） |
