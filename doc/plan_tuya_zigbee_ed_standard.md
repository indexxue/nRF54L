# 计划：zigbee_ed 涂鸦标准接入（方案 B）

> **For agentic workers:** REQUIRED SUB-SKILL: Use `executing-plans` or subagent-driven-development to implement task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让现有 `projects/zigbee_ed` 作为**标准 Zigbee On/Off 灯**，能被涂鸦多模网关发现、绑定，并在涂鸦智能 / Smart Life App 中开关板载绿灯；同时不破坏与 ZigBridge-EM 的 ZCL 联调契约。

**Architecture:** 子设备继续只跑 Nordic ZBOSS / 标准 ZCL（EP1 · Basic + Identify + OnOff）。涂鸦云与 App 只经**涂鸦 Zigbee 网关**；本仓**不**接涂鸦模组 UART、**不**在 ED 内实现 MQTT。先对齐涂鸦品类模板的设备身份与 OnOff 行为，再用真网关+App 验收；ZigBridge 仅作回归对照。

**Tech Stack:** nRF54L15 · NCS 3.2 + ncs-zigbee v1.4 · `projects/zigbee_ed` · 涂鸦 IoT 平台 · 涂鸦 Zigbee&蓝牙-WiFi 多模网关 · 涂鸦智能/Smart Life

**Spec / 背景：**
- 联调基线：[`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md)
- 配网行为：[`zigbee_demo_pairing_control.md`](zigbee_demo_pairing_control.md)
- 工程入口：[`../projects/zigbee_ed/README.md`](../projects/zigbee_ed/README.md)
- 决策：方案 **B 标准接入**（非模组）；网关选用涂鸦多模网关

## 门禁（开干前对齐）

```text
【需求】zigbee_ed 标准 Zigbee 接入涂鸦：网关发现/绑定 + App 开关灯；不接模组 UART
【上下文】ZBOSS 信号/ZCL 回调只 ingress；灯效与配网 UX 在 src/app.c；协议在 lib/zigbee/
【能否阻塞】Steering/rejoin 可秒级；GATT/ISR/回调禁止 sleep/刷屏/写 Settings；失败对端=App 添加失败或开关无响应
【队列/锁】沿用现有 s_zb_evt_q；入队 K_NO_WAIT；仅消费者阻塞；不新开业务线程（v1）
【Flash】网络身份走 zboss_nvram；Basic 字符串用编译期常量或 Settings 事件写（禁止周期写）
【共存】忙时配网/控灯路径可用；ZigBridge znp light 回归必须仍过；量产禁止本机 Coord
【验收】涂鸦 App 开关 → LED_G + 串口 light: on|off；串口见 joined；ZigBridge 对照组仍可控
【合入】实验分支先合；调试 LOG/TRACES 量产关闭；PID/型号未定时用占位宏，禁止写死虚假量产身份
```

## Global Constraints

- 角色固定 **End Device**；禁止 Coordinator；厂测 Coord 只在 `projects/factory`
- 空中契约保持：EP **1** · OnOff **`0x0006`** · On/Off/Toggle `0x01/0x00/0x02`
- 子设备**不**解析涂鸦串口协议、**不**实现 MQTT
- 命名：`module_verb_object()`；非必要不建新文件；优先改 `zigbee_app.*` / `zigbee_device.h` / `src/app.c` / README
- 引脚走现有 overlay；分区不因本计划改动
- 涂鸦 PID / manufacturer / model 以平台建品后的**真实值**为准；未拿到前用 `CONFIG`/宏占位并在 README 标明

## Review Focus

- 涂鸦网关拒绝入网 / 绑定成「未知设备」→ Basic manufacturer/model 与平台产品不一致
- App 能添加但开关无效 → EP/Cluster 与品类映射不符，或 OnOff Server 未响应
- App 开关后状态不同步 → 缺 attribute report / 读属性路径
- 入网窗对不齐 → App 添加超时 vs 本机 60s Steering 窗
- 修涂鸦后 ZigBridge 控灯回归失败 → 破坏了 EP1/OnOff 契约

## 非目标（本计划不做）

- 涂鸦模组方案 / MCU 串口 DP 协议
- 改 ZigBridge-EM 或接涂鸦云云对接
- Level / 色温 / 场景等多 DP（可后续另开计划）
- Sleepy ED 功耗优化定稿（可列后续）
- Zigbee 官方认证证书包、量产授权码流水线终态
- BLE+Zigbee 同镜像共存

## 文件落点

| 文件 | 职责 |
|------|------|
| `projects/zigbee_ed/lib/zigbee/zigbee_app.c` | Basic 属性填充、OnOff 上报/读、起栈身份 |
| `projects/zigbee_ed/lib/zigbee/zigbee_app.h` | 对外 API 不变为主；必要时增加 identity 配置宏说明 |
| `projects/zigbee_ed/lib/zigbee/zigbee_device.h` | 端点/簇声明；仅当模板要求增簇时改 |
| `projects/zigbee_ed/prj.conf` / `Kconfig` | manufacturer/model/功率等可配置项 |
| `projects/zigbee_ed/src/app.c` | 配网按键 / LED；不塞 ZCL 细节 |
| `projects/zigbee_ed/README.md` | 涂鸦验收步骤 + 占位身份说明 |
| `doc/tuya_ed_zcl_mapping.md`（新建） | 平台 PID/DP ↔ ZCL 对照表（平台侧产出物落仓） |

```text
手机 App ──云──► 涂鸦多模网关 ──ZCL──► zigbee_ed (EP1 OnOff)
                     ▲
                     └── 验收主路径

ZigBridge-EM ──znp light──► 同一固件          ← 回归对照，非涂鸦必需
```

## 里程碑总览

| ID | 里程碑 | 出口标准 |
|----|--------|----------|
| T0 | 平台建品 + 映射表落仓 | `doc/tuya_ed_zcl_mapping.md` 有 PID/DP/ZCL |
| T1 | Basic 身份可配置 | 串口/读属性可见 manufacturer/model |
| T2 | OnOff 对涂鸦可控可上报 | App 开关灯；状态能回读/上报（按映射要求） |
| T3 | 配网窗与 App 添加对齐 | 常规添加流程可重复成功 ≥3 次 |
| T4 | 双路径验收文档 | 涂鸦主路径 + ZigBridge 回归 checklist 写入 README |
| T5 | 发布联调固件 | `release` 版本号 + `firmware/` 说明 |

---

### Task 0: 涂鸦平台建品与映射落仓（阻塞固件定值）

**Files:**
- Create: `doc/tuya_ed_zcl_mapping.md`
- Modify: `projects/zigbee_ed/README.md`（链到映射表）

**Interfaces:**
- Consumes: 涂鸦 IoT 后台品类模板
- Produces: 文档字段 `pid`、`product_name`、`manufacturer_name`、`model_id`、DP 表（至少 switch ↔ OnOff）

- [x] **Step 1: 在涂鸦 IoT 创建 Zigbee 标准灯/开关品类产品**（2026-11-10）

光源 · 自定义 · Zigbee · 产品名 `zigbee_ed` · PID `agbfpcdx`；硬件开发选**自定义固件**（非模组）。

- [x] **Step 2: 导出或手抄 DP ↔ ZCL 映射**（2026-11-10）

xlsx + 映射表 §3.2；本期仅 DP1 `switch_led`。平台未给 manufacturer/model → 联调占位 `TY` / `zigbee_ed`。固件 key=`keyanydv`。

- [x] **Step 3: 写入 `doc/tuya_ed_zcl_mapping.md`**

- [x] **Step 4: README 增加「涂鸦标准接入」一节链接该文档**

- [x] **Step 5: 门禁确认**

**T0 关闭（2026-11-10）。** 下一任务 **T1** Basic 身份。

---

### Task 1: Basic 设备身份（manufacturer / model / 版本）

**Files:**
- Modify: `projects/zigbee_ed/lib/zigbee/zigbee_app.c`（Basic attr 声明与 init）
- Modify: `projects/zigbee_ed/Kconfig` 与/或 `prj.conf`（字符串/版本可配）
- Modify: `projects/zigbee_ed/lib/zigbee/zigbee_device.h`（仅当需换 Basic attr 宏）

**Interfaces:**
- Consumes: Task 0 的 `manufacturer` / `model`（或占位）
- Produces: 入网后协调器/网关可读到的 Basic 属性；串口启动日志打印身份摘要

- [x] **Step 1: 扩展 Basic 属性列表**（2026-11-10）

`ZB_ZCL_DECLARE_BASIC_ATTRIB_LIST_EXT`：manufacturer / model / app_version / sw_build 等。

- [x] **Step 2: 用 Kconfig/宏注入字符串**（2026-11-10）

`CONFIG_ZIGBEE_ED_MANUFACTURER`=`TY` · `CONFIG_ZIGBEE_ED_MODEL`=`zigbee_ed` · `CONFIG_ZIGBEE_ED_APP_VERSION`。

- [x] **Step 3: 起栈日志打印身份**（2026-11-10）

`zigbee: id mfr=… model=… app_ver=… sw=…`

- [x] **Step 4: 编译**（2026-11-10 · `nrf.cmd -Project zigbee_ed build` 通过）

板测（flash + 串口 + ZigBridge `znp light`）待你上板勾选。

- [ ] **Step 5: Commit（仅当用户要求提交时）**

**验收:** 读 Basic 可见与映射表一致（或明确占位）；ZigBridge 控灯不回归。  
**T1 软件出口已完成；板测请本地 flash 确认。**

---

### Task 2: OnOff 对涂鸦可控 + 状态可读/可报

**Files:**
- Modify: `projects/zigbee_ed/lib/zigbee/zigbee_app.c`（OnOff set 回调后 report / 读路径确认）
- Modify: `projects/zigbee_ed/src/app.c`（若需区分「网络灯效」与业务灯，保持绿灯=业务）

**Interfaces:**
- Consumes: 现有 `ZIGBEE_APP_EVT_LIGHT` / `light_on`
- Produces: App 下发 On/Off 后本地灯态正确；网关读 OnOff attr `0x0000` 与灯态一致；若模板要求 report，则状态变化有 report

- [x] **Step 1: 对照 Task 0 映射**（2026-11-10）

DP1 `switch_led` ↔ OnOff cmd + attr `0x0000`（rw）。

- [x] **Step 2: OnOff 属性同步 + 默认 report**（2026-11-10）

入网 `zigbee_app_onoff_report_setup()`；`zigbee_app_onoff_set_value()` 同步 attr 并驱灯（evt 队列）。编译通过。

- [ ] **Step 3: 板测（ZigBridge 先）** ← 待你

`znp light` → `zigbee: zcl onoff=` + `light:`；入网日志见 `onoff report put/start`。

- [ ] **Step 4: 板测（涂鸦网关）** ← 网关到手后

**验收:** 涂鸦 App 可开关；面板状态与灯一致；ZigBridge 回归通过。

---

### Task 3: 配网 UX 与涂鸦 App「添加设备」对齐

**Files:**
- Modify: `projects/zigbee_ed/src/app.c`（按键提示/超时若需）
- Modify: `projects/zigbee_ed/lib/zigbee/zigbee_app.c`（仅当需可配置配网窗）
- Modify: `projects/zigbee_ed/README.md`

**Interfaces:**
- Consumes: `zigbee_app_steer_request()` / `zigbee_app_leave_request()`
- Produces: 文档化的「App 点添加 → 本机 BTN1」时序；可配置窗长（可选 Kconfig）

- [ ] **Step 1: 实测涂鸦 App 添加倒计时**

记录 App 默认开窗时长；与本机 `ZIGBEE_APP_STEER_WINDOW_MS`（60s）对比。

- [ ] **Step 2: 必要时把配网窗改为 Kconfig**

保持默认 60s；若 App 更短，文档要求「先按键再点添加」或加长窗。

- [ ] **Step 3: 固化操作顺序到 README**

```text
1. 网关已在 App 在线
2. App：添加设备 → 选网关/灯类
3. 本机：BTN1 短按开配网窗（蓝慢闪）
4. 对照 IEEE / App 发现列表
5. 绑定成功后 App 开关灯
失败：BTN2 双击 leave 或长按擦 NV 后重来
```

- [ ] **Step 4: 重复添加 ≥3 次（含 leave 后再入）**

**验收:** 按 README 可独立复现入网；失败有明确 leave/清 NV 恢复手段。

---

### Task 4: 双路径验收清单与版本发布

**Files:**
- Modify: `projects/zigbee_ed/README.md`
- Create/Update: `firmware/<ver>/README.md`（发布时）

**Interfaces:**
- Consumes: T0–T3 出口
- Produces: 可交给测试的 checklist；带版本号的联调固件说明

- [ ] **Step 1: README 增加两节**

1. **涂鸦标准接入验收**（主）  
2. **ZigBridge 回归**（对照，命令保持现有 `znp join` / `znp light`）

- [ ] **Step 2: Checklist（必须全勾才算 T4 过）**

涂鸦：

- [ ] 网关上云  
- [ ] 子设备加入并绑定到本 PID 产品  
- [ ] App on → LED 亮 / `light: on`  
- [ ] App off → LED 灭 / `light: off`  
- [ ] 子设备重启后仍在网且可再控（或文档写明需 rejoin 行为）  
- [ ] leave 后再添加成功  

ZigBridge：

- [ ] `znp join` + IEEE 一致  
- [ ] `znp light on|off|toggle`  
- [ ] 同网掉电 rejoin（契约 §2.3）抽测  

- [ ] **Step 3: `nrf.cmd -Project zigbee_ed release <ver>` 并写 firmware 说明**

**验收:** 测试同学只靠 README + 映射表能跑通涂鸦主路径。

---

### Task 5:（后续可选，另开或本计划末尾排期）

不阻塞「App 能开关灯」首达：

| 项 | 说明 |
|----|------|
| Sleepy ED | 电池标签时再改 `rx_on_when_idle` / 轮询 |
| Install Code | 按涂鸦安全策略 |
| Identify 闪灯 | 若 App「识别设备」需要 |
| 多 DP | Level 等新计划 |
| 产测号段 / 授权 | 与涂鸦产线文档对齐 |
| OTA | 对齐 `flash_layout` / MCUBoot |

---

## 风险与回滚

| 风险 | 缓解 |
|------|------|
| 品类模板与标准 HA OnOff Light 不完全一致 | Task 0 先锁映射；差 cluster 再改 `zigbee_device.h` |
| 涂鸦网关对 manufacturer/model 敏感 | 身份与平台一字不差；禁止随意改 |
| 改 report 影响功耗/总线 | 仅状态变化 report；联调期可先 rx_on |
| 破坏 ZigBridge | 每任务保留 `znp light` 回归 |

回滚：恢复 Basic 最小属性列表与原 OnOff 路径；涂鸦相关 Kconfig 默认关闭或回占位。

## 建议排期（人天量级，供排期）

| 任务 | 估时 |
|------|------|
| T0 建品+映射 | 0.5–1（含等平台权限） |
| T1 Basic 身份 | 0.5–1 |
| T2 OnOff/report | 1–2 |
| T3 配网对齐 | 0.5–1 |
| T4 文档+release | 0.5 |
| **首达 App 控灯** | **约 3–5 人天**（网关到手且 PID 就绪前提下） |

---

## 修订记录

| 日期 | 说明 |
|------|------|
| 2026-11-10 | 初稿：方案 B 标准接入；多模网关；基于 zigbee_ed 0.2.3 基线 |
| 2026-11-10 | T0 仓内：新增 `tuya_ed_zcl_mapping.md`；README 挂接；平台建品仍待账号 |
| 2026-11-10 | **T0 关闭**：PID/DP/firmware key 齐；mfr/model 联调占位 `TY`/`zigbee_ed` |
| 2026-11-10 | **T1 软件完成**：Basic EXT + Kconfig；build 通过；待板测 |
| 2026-11-10 | **T2 软件完成**：OnOff report setup + set_value；build 通过；待板测 |
