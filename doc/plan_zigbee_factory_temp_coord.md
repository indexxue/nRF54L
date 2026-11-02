# 计划：厂测临时 Coord 通道（双机互读）

> 背景：协调器不可用、现场只剩两台子设备时，用**厂测固件**把一台临时建成 Coord，另一台入临时网后读数。  
> **不影响**量产 `zigbee_ed` / `zigbee_router`（仍禁止本机 Coordinator）。  
> 对照：[`dev_norms.md`](dev_norms.md)、[`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md)、[`projects/zigbee_ed/README_zigbee_system.md`](../projects/zigbee_ed/README_zigbee_system.md)。

## 门禁（开干前对齐）

```text
【需求】厂测后门：不拆机互读。同 hex 双机 TOOL(临时Coord)+UUT(临时DUT)；allow 后 zbcmd；非「切角色产品」
【上下文】串口 zb tool|uut；栈未起则原地起角色；已起再切才清NV+复位；回调只 ingress
【能否阻塞】form/steer 秒级可接受；失败 ng；不堵串口解析
【队列/锁】ZBOSS + 短 work；v1 不新开复杂队列；入队 K_NO_WAIT
【Flash】上电清 zboss 网 NV；Settings 保留；角色切换才额外擦+复位
【共存】量产 zigbee_ed 零 Coord；本地串口与空中代理同 cmd 表（M3）
【验收】TOOL: zb tool → formed（无复位）；UUT: zb uut → joined；切角色才 switch_reboot
【合入】projects/factory；量产 ed/router 不改
```

## 方案 R1（已锁定，2026-11-02）

```text
语义：TOOL=维修/夹具侧临时 Coord；UUT=被测侧（当前实现为 Router，因单 hex 非ED库）
上电：wipe zboss → mode=idle（无 RF）→ 显式 zb tool | zb uut
首次选角色：原地起栈，不复位
TOOL↔UUT 切换：写 req + wipe + 冷复位（唯一需要复位的角色路径）
别名：coord→tool，dut→uut（兼容旧串口习惯）
不做：量产隐式 Coord；双 factory 产物；每次 zb tool 都复位
```

## 目标

1. 两台烧**同一个 factory 厂测产物（单 hex/单 release）**的设备，无外部协调器时能临时组网。  
2. Coord 侧能对 DUT **下发/代理全部可控厂测命令**（与本地串口厂测命令对齐，见 §厂测命令范围）。  
3. 角色选择为**显式厂测通道**（串口），非量产隐式后门。  
4. **重启（或每次上电）只清除临时 Zigbee 网信息**；应用 Settings 保留。  
5. 量产 `zigbee_ed` / `zigbee_router` **不**增加 Coordinator 能力。  
6. **禁止双产物**：不得拆成 `*_coord` / `*_dut` 两套 release / 两套量产级 hex。  
7. **不新建** `zigbee_factory` 工程；能力全部做进现有 [`projects/factory`](../projects/factory/)。

## 非目标（本计划不做）

- 量产固件运行时 ED/Router ↔ Coord 热切换  
- 不清 NV、不解旧网密钥去“接管”仍挂在真网关下的设备  
- 替代 ZigBridge-EM 做日常建网 / OTA / 正式配网  
- 发布两套厂测固件产物（Coord / DUT 分包）  
- 新建独立 `projects/zigbee_factory`（已否决；共用 `factory`）  
- 信道自适应 / 自动避让  
- 真 ED-UUT（单 hex 已用 Router-UUT；另库再议）  
- 本期强制走量产整包 hex（现阶段按现有 factory 烧录流程即可）

## 已对齐决策

| 项 | 决策 |
|----|------|
| 形态 | **仅厂测固件**；量产零 Coord |
| 双方固件 | 两台都跑 **factory**；**同一镜像 / 同一 release 产物** |
| 产物约束 | **禁止双产物**；模式靠运行时选择（必要时同镜像内软复位切模式，模式字节可放 retained/专用标志，**不**另出第二份固件） |
| 工程落点 | **共用**现有 `projects/factory`（增强 `lib/cmd` + 后续 Zigbee）；**不**新建 `zigbee_factory` |
| 临时网生命周期 | **上电/重启清除** Zigbee 网络身份（密钥、短地址、parent 等） |
| 应用 Settings | **重启后不清**（sn/did 等保留）；与清网解耦 |
| 控制面 | Coord 代理 **全部可控厂测命令** 到 DUT（非只读最低集） |
| 控制关系 | 网络角色：临时 Coord vs DUT(ED)；应用层：命令通道 + ZCL/厂商 cluster |
| 入网保障 | 主关键字 **IEEE**；Coord 须先 `zb allow ieee`；未登记/IEEE 不匹配 → **拒绝入网** |
| sn 核对 | 可选 `zb allow sn`；入网后 `zbcmd sn` 比对，**不符仅告警、业务继续**（不强制踢除） |
| 白名单容量 | **暂定仅 1 台**；新 `zb allow ieee` 覆盖旧值（或先 `clear`）；多目标不进本期 |
| 与真网关系 | 临时 **另建 PAN**；不继承现场原网 |
| 信道 | **默认 ch15**；串口可改（如 `zb ch <11..26>`）；**不做**自适应选信道 |
| 模式入口 | **仅串口**（`zb tool` / `zb uut`；别名 coord/dut）；不做按键切模式 |
| 角色模型 | **R1**：TOOL/UUT 后门语义；首次原地起栈；切换才复位 |
| UUT 栈角色 | 本期 **Router**（单 hex 非 ED 库）；真 ED 需另库，后续再议 |
| 烧录阶段 | **现阶段**按现有 `factory` 流程（slot1 / `nrf.cmd -Project factory …`）；量产后整包 hex（**bootloader + app + factory**） |
| 流程图格式 | 计划内流程一律用 **纯 text ASCII**（不用 Mermaid），保证各编辑器可直接评审 |
| ccfg（APPROTECT） | 见下节 **ccfg 策略**；仅保护 app 调试读；**固件不上电自动 lock** |

## ccfg 策略（已确认）

目标：单独烧 app / 单独烧厂测都能正常下载；保护由人/产线/OTA 显式打开，不偷偷自动开。

```text
1) 日常开发烧录（hex / J-Link）
   · 默认不依赖「已 lock」；单独烧 slot0 APP 或单独烧 factory 均可正常烧录
   · 「烧录 hex 时开启」指：产线/发布烧录流程可在写完镜像后执行 lock
     （工具侧或烧录脚本），不是固件每次上电自动 lock

2) 厂测固件运行时
   · 不上电自动 ccfg lock
   · 用户可用串口主动：ccfg lock / ccfg（查状态）
   · 用户若将保护关掉（或处于 unlock）：固件不得自动再开
   · 是否开启交由用户在厂测手动决定

3) OTA（后续）
   · OTA 功能启用并完成合法升级后，允许在 OTA 成功路径里自动 ccfg lock
   · 仍不动 ERASEPROTECT；仅 APPROTECT（app 调试读保护）

4) unlock
   · 应用内 ccfg unlock 不能真正清 UICR → 回 need_recover
   · 真解锁靠调试器 recover / 擦保护相关区
```

## 沟通结论（Q1–Q8 已全部关闭）

| # | 结论 |
|---|------|
| Q1 / 工程落点 | **共用 `projects/factory`**；不新建 `zigbee_factory`（脚手架已撤） |
| Q2 | **只要单产物**；禁止 Coord/DUT 双 release |
| Q3 | 覆盖 **全部可控厂测命令**（见下节），不限于 sn/version |
| Q4 | 应用 Settings **重启不清**；只清 Zigbee/ZBOSS 网络 NV |
| Q5 | 默认 **ch15**，支持串口修改；暂不做自适应 |
| Q6 | **仅串口** |
| Q7 | 先做 **ED**，后续再补 Router DUT |
| Q8 | 现阶段走现有 **factory 烧录**；量产后整包 `bootloader+app+factory` hex |
| 身份 | 主关键字 **IEEE**；sn 不符 **告警继续**；白名单 **暂定仅 1 台** |

## 厂测命令范围（Q3）

原则：**凡本厂测固件本地串口可执行、且语义上允许对远端 DUT 操作的命令，Coord 均须能经空中下发并回显结果。**

基线对齐现有 `projects/factory/lib/cmd` 默认集，并随本工程 `cmd_register` / project 扩展表同步增长：

| 命令 | 本地（A/B 串口） | Coord → DUT 空中代理 | 说明 |
|------|------------------|----------------------|------|
| `help` | 要 | 可选（列远端已注册表） | |
| `version` | 要 | **要** | |
| `sn` / `sn <value>` | 要 | **要**（读/写） | 写 sn 落应用 Settings，重启保留 |
| `mac` / `mac <HEX>` | 要 | **要** | 连续大写十六进制，无冒号（如 `987A6BC5…`，8 字节=16 hex） |
| `led on\|off` / `led r\|b\|all on\|off` | 要 | **要** | 对齐 STM32 厂测语义 |
| `btn` / `btn ftm` | 要 | **要** | 返回逻辑电平 `0`/`1` |
| `ccfg` / `ccfg lock\|unlock` | 要 | **local_only** | 仅 APPROTECT；**不自动开**；厂测手动 lock；OTA 成功后可自动 lock；unlock→need_recover |
| `log` | 要 | **要** | DUT 打一条 log，Coord 回 `ok`/`ng` |
| `reboot` | 要 | **要** | 远端复位；清的是 DUT 临时网，Settings 保留 |
| `ftmexit` | 要 | **要** | boot_mode=app + 复位 |
| `zb` | 要 | **要** | `zb tool\|uut\|status\|ch\|allow\|join`（别名 coord/dut；`[local]`） |
| `zbcmd` | 要 | **要** | `zbcmd <factory-line>` 空中代理（`[local]`；已实现） |
| 后续 `cmd_register` 的厂测命令 | 要 | **默认要** | 确不可远程的须 `cmd_register_local` |

实现（M3 已落地）：

- 自定义 ZCL cluster `0xBEEF`：REQ=命令行字符串，RSP=回包。  
- UUT：`k_work` → `cmd_process_line_remote`（与本地同表）→ 回传。  
- TOOL：`zbcmd <line>` 发往已 allow+joined 的唯一 DUT；串口 `zbcmd:<远端回复>`。  
- 禁止第二套命令语义。

## 实现逻辑图（供评审）

> 本节全部为 **纯 text**，避免 Mermaid 在部分预览器中格式错乱。

### 0. 操作总览（现场怎么做）

```text
准备：两台烧同一 factory hex（.\nrf.cmd -Project factory flash → slot1）

  ① 两侧上电
       → 只清 Zigbee 网 NV；Settings(sn/…)保留；mode=idle（无 RF）
       → zbft: net wipe boot / zbft: settings kept

  ②（可选）两侧：zb ch <ch>          // 默认 15；须在起栈前；不做自适应

  ③ 设备B：zb uut
       → 原地起 Router-UUT；串口直接回 ieee=<16hex>（不必再 zb status）

  ④ 设备A：zb tool
       → 原地起 TOOL；form；**不**自动开窗

  ⑤ 设备A：zb join <B的ieee>         // 最简：allow + 开窗 60s
       （等价：zb allow ieee <hex> 自动开窗；zb join [sec] 仅重开）
       （可选）zb allow sn <value>     // 入网后 zbcmd sn 核对；不符只告警

  ⑥ B 自动 Steering
       → IEEE 匹配 → uut joined / A：dut joined
       → 未 allow / 不匹配 → A：zbft: join deny

  ⑦ 两侧 zb status：同 ch/同 pan；allow= / joined=

  ⑧ 设备A：zbcmd version|sn|log|…
       → 只对已 allow 且 joined；超时/未入网 → zbcmd:ng
       → 若曾 zb allow sn：不符 zbft: sn warn，**不中断**后续 zbcmd

  ⑨ reboot/掉电 → 临时网与 allow 会话作废；Settings 仍在；须重新 ③～⑧
```

### 1. 入网保障（身份门禁）——本期硬约束

```text
目的：设备A「添加设备」前必须明确知道对端是谁，禁止匿名 join。

  主关键字：IEEE（入网瞬间栈必带，硬匹配；已确认）
  辅关键字：sn（可选；入网后核对）

  规则（已确认）：
    · 未执行 zb allow ieee → 不得成功加入任何 DUT
    · 入网请求 IEEE ≠ allow.ieee → deny
    · 白名单容量暂定 = 1；再次 zb allow ieee 即覆盖上一台（或先 clear）
    · zbcmd 只打向「已 allow 且已 joined」的唯一目标；无目标 → ng
    · 若设置了 allow.sn：zbcmd sn 结果与之不符 → zbft: sn warn（告警）+ 业务继续
      （不因 sn 不符强制踢除）
```

入网判定（A 侧）：

```text
                    DUT 发起 Association
                            |
                            v
                   +------------------+
                   | 已 zb allow ieee？ |
                   +--------+---------+
                            |
              否 ------------+------------ 是
              |                            |
              v                            v
     zbft: join deny              IEEE == allow.ieee ?
     （不开业务会话）                     |
                         否 -------------+------------- 是
                         |                             |
                         v                             v
                zbft: join deny              接受入网 / joined
                                             zbft: dut joined ieee=...
                                                   |
                                                   v
                                          （可选）比对 allow.sn
                                           否 → zbft: sn warn（继续）
                                           是 → zbft: sn match
                                                   |
                                                   v
                                             允许 zbcmd 业务
```

### 2. 双机时序（主路径，纯文本）

```text
操作员          设备A(串口/栈)              空中               设备B(串口/栈)
  |                  |                      |                     |
  | 上电             |                      |              上电   |
  |----------------->|                      |<--------------------|
  |            清网NV/留Settings            |        清网NV/留Settings
  |            mode=IDLE                    |        mode=IDLE
  |                  |                      |                     |
  | zb status(B) 读 IEEE/sn                 |                     |
  |<----------------------------------------|---------------------|
  |                  |                      |                     |
  | zb coord          |                      |                     |
  |----------------->| form(ch 默认15)      |                     |
  |                  |  (尚无白名单=拒join)  |                     |
  | zb allow ieee ... |                      |                     |
  |----------------->| 写入 allow 表        |                     |
  | zb join     |                      |                     |
  |----------------->| permit join(门禁开)  |                     |
  |                  |                      |                     |
  |                  |                      |  zb dut              |
  |                  |                      |<--------------------|
  |                  |                      |  Steering           |
  |                  |     Association      |                     |
  |                  |<---------------------|                     |
  |                  | IEEE 匹配？          |                     |
  |                  |--否--> deny          |                     |
  |                  |--是--> accept ------->| joined              |
  |                  |                      |                     |
  | zbcmd version    |                      |                     |
  |----------------->| ----- 命令帧 ------->| cmd_process_line    |
  |<-----------------| <---- ok+value ------|                     |
  |                  |                      |                     |
  | zbcmd reboot     | ----- reboot ------->| 复位(网清/Settings留)|
  |                  |  会话结束,须重跑门禁  |                     |
```

### 3. 单机状态机（同一 hex）

```text
                    +--------+
                    |  Boot  |
                    +---+----+
                        | 上电：清网 NV；Settings 保留
                        v
                    +--------+
              +---->|  IDLE  |<-----------------------------+
              |     +---+----+                               |
              |         |                                    |
              |         | zb coord          zb dut             |
              |         v                    v               |
              |   +-----------+        +-----------+         |
              |   | COORD_FORM|        | DUT_STEER |         |
              |   +-----+-----+        +-----+-----+         |
              |         | form ok              |             |
              |         v                      |             |
              |   +-----------+                |             |
              |   | COORD_READY|               |             |
              |   | (无allow则 |               |             |
              |   |  拒一切join)|              |             |
              |   +-----+-----+                |             |
              |         | zb allow ieee         |             |
              |         v                      |             |
              |   +-----------+                |             |
              |   | COORD_ALLOW|               |             |
              |   | +joinwindow|               |             |
              |   +-----+-----+                |             |
              |         | 匹配IEEE的入网       |             |
              |         v                      v             |
              |   +-----------+        +-----------+         |
              |   | COORD_BIND|<------>| DUT_JOINED|         |
              |   | zbcmd 代理 |        | 执行cmd表 |         |
              |   +-----+-----+        +-----+-----+         |
              |         |                    |               |
              +---------+--- reboot/掉电 ----+---------------+
```

### 4. zbcmd 代理路径（命令不双写）

```text
  设备A COORD                         设备B DUT
  -----------------                   -----------------
  串口: zbcmd sn
       |
       v
  有 allow且已joined? --否--> 串口 ng
       |
      是
       |
       v
  组空中命令帧  ----------------->  收帧, 取 line 字符串
                                       |
                                       v
                                  cmd_process_line(line)
                                  (与本地串口同一 cmd 表)
                                       |
                                       v
  串口打印 ok|ng  <---------------  组回包
```

### 5. 清网 vs Settings（重启语义）

```text
  reboot / 掉电
       |
       v
  再次上电
       |
       +---> Zigbee 网 NV     → 清除（密钥/短址/parent）
       +---> 应用 Settings    → 保留（sn/did/…）
       +---> zb allow 会话     → 清除（须重新登记 IEEE）
       +---> mode             → IDLE
       |
       v
  须重新：B zb uut → A zb tool → A zb join <ieee>
```

### 6. 架构示意

```text
  +---------------------------+     临时PAN + 身份门禁      +---------------------------+
  | 设备A MODE_COORD          |                            | 设备B MODE_DUT (ED)       |
  | form + zb allow(IEEE)      |<---- join(仅白名单) ------>| Steering                  |
  | zbcmd 代理厂测命令         |<---- 命令/回包 ----------->| 同一 cmd 表执行            |
  | 串口: 状态 / 远端 ok|ng   |                            | 串口: IEEE/sn / joined    |
  +---------------------------+                            +---------------------------+
           ^                                                          ^
           | 上电: 只清网 NV + 清 allow 会话; Settings 保留            |
           +---------------- 临时网不可跨重启自动恢复 -----------------+
```

### 模块边界（拟）

| 模块 | 职责 | 禁止 |
|------|------|------|
| `projects/factory` | 厂测入口、串口 cmd、后续 Zigbee 双模式 | 链进量产 ed/router；拆第二份 release；另建 zigbee_factory |
| `projects/factory/lib/cmd` | 统一命令表；本地与空中共用（S0 增强） | 空中旁路另一套语义 |
| `projects/factory/lib/zigbee/*`（后续） | Coord 建网 / DUT Steering / 命令通道状态机 | 回调里 sleep/写 Settings |
| `common/nvs` | sn/did 等；**不**用 Settings 存临时 PAN | 启动清网时误擦应用键 |
| 量产 `zigbee_ed` / `zigbee_router` | 无改或仅 README 链到本计划 | 增加 `CONFIG_ZIGBEE_ROLE_COORDINATOR` |

### 启动策略（单产物硬约束）— **已按 R1 落地**

1. `zbft_boot`：**只清 zboss 网 NV**；**不清**应用 Settings；默认 **idle（无 RF）**。  
2. `zb tool` / `zb uut`：栈未起 → **原地起栈**；已起再切角色 → Settings `zbft/req` + wipe + 冷复位。  
3. TOOL：form（默认 ch15）；**不**自动开窗；无 allow 时开窗也拒未知 IEEE。  
4. `zb join <ieee>` / `zb allow ieee`：登记唯一 DUT 并开窗；`DEVICE_ANNCE` 比对，mismatch → leave + `join deny`。  
5. UUT：Router Steering（3s 短重试，配对中不刷日志）；匹配 → `uut joined`。  
6. `zbcmd`：自定义 ZCL cluster `0xBEEF` 传命令行字符串；UUT `cmd_process_line_remote`；TOOL 等回包（超时 ng）。  
7. `reboot` / 掉电：网 + allow 会话不恢复；Settings 仍在；须重新 tool/uut/join。

> **M0 已过**：单一 factory 产物内 TOOL+UUT；Kconfig=`COORDINATOR` 非 ED 库 → UUT=Router。

## 串口契约（已实现）

| 命令 | 谁用 | 行为 |
|------|------|------|
| `help` | 双方 | 列出组网命令 + 厂测命令；`[local]` 可见 |
| `zb tool`（别名 `coord`） | A | 进 TOOL 并 form；**不**自动开窗；首次无复位 |
| `zb uut`（别名 `dut`） | B | 进 UUT 并 Steering；**直接回 ieee=** |
| `zb status` | 双方 | mode / pan / ch / nwk / ieee / allow / joined / sn |
| `zb ch [11..26]` | 双方（起栈前） | 默认 15；已起栈 → `busy` |
| `zb allow ieee\|sn\|clear` | TOOL | 唯一白名单；allow ieee 已 formed 时自动开窗 |
| `zb join <ieee>` / `zb join [sec]` | TOOL | `<ieee>`=allow+开窗；`[sec]` 重开；无 allow → ng |
| `zbcmd <factory-line>` | TOOL | 代理到已 allow 且 joined 的 DUT；失败 `zbcmd:ng` |
| `reboot` / `version` / `sn` / `log` / … | 双方本地；可控集可远程 | 与 factory cmd 语义一致 |

日志关键字（验收用）：

```text
zbft: mode=tool|uut (router)|idle
zbft: net wipe boot
zbft: settings kept
zbft: formed ch=15 pan=0x....
zbft: allow ieee=................
zbft: join open 60 s
zbft: join deny ieee=................ reason=no_allow|mismatch
zbft: dut joined ieee=... nwk=0x....
zbft: uut joined nwk=0x.... pan=0x....
zbft: sn warn expect=... got=...          # 不符仍继续
```

## 里程碑

### S0 — 厂测 cmd 框架（落在 `factory`）

- [x] **不**新建工程；在现有 `projects/factory/lib/cmd` 上增强。  
- [x] 增强：`cmd_register_local` / `local_only`、`cmd_process_line_remote`（capture + 拒 local）、help 标 `[local]`。  
- [x] 默认可控集（均可远程）：`help` / `version` / `sn` / `sn <v>` / `log` / `reboot` / `ftmexit` / `mac` / `led` / `btn`。  
- [x] `zb` / `zbcmd` / `ccfg` 为 `[local]`。  
- [x] 板测：串口 help / version / sn / log 可用（S0 已过）。

### M0 — 单产物双角色可行性（硬门槛）

- [x] 在 **`projects/factory`** 上接入 Zigbee；同一 factory 工程能编出**一个**可运行镜像。  
- [x] 板测：`zb tool` → formed；`zb uut` → Steering / joined（双机已过）。  
- [x] 机制 **R1**：首次原地起栈；切换才 Settings `zbft/req` + wipe + 冷复位；Kconfig=`COORDINATOR`。  
      **UUT = Router**（非 ZED）：单 hex 非 ED 库。  
- [x] **交付物仍是一份** factory 产物；不接受 coord/dut 两包。  
- [x] 上电：`zbft: net wipe boot` + `zbft: settings kept`；默认 idle。

### M1 — 组网入口骨架

- [x] 上电 `zbft: net wipe boot` + `zbft: settings kept`。  
- [x] `zb status` / `zb tool|uut`（别名 coord/dut）/ `zb ch` 已注册可用。

### M2 — 双机组网 + 身份门禁

- [x] TOOL：默认 ch15 form；未 allow 时不开业务入网；错 IEEE → `join deny`。  
- [x] `zb allow ieee` / `zb join <ieee>`：容量=1；覆盖旧白名单；自动开窗。  
- [x] 可选 `zb allow sn`（核对在 M3 `zbcmd sn`）。  
- [x] `zb ch` 起栈前可改；两侧同 ch 板测通过。  
- [x] UUT：Router Steering；匹配后 `uut joined` / `dut joined`。  
- [x] 重启后临时网与 allow 不恢复（RAM 会话）。  
- [x] **不做**自适应选信道；多白名单不进本期。

### M3 — 厂测命令空中代理

- [x] DUT：自定义 ZCL `0xBEEF` + `cmd_process_line_remote`（与本地同表）。  
- [x] TOOL `zbcmd`：version / sn / log / reboot / ftmexit 等可控集。  
- [x] 新注册默认可远程；`zb`/`zbcmd`/`ccfg` 为 `local_only`，help 可见 `[local]`。  
- [x] 超时 / 未 joined → `zbcmd:ng`（不假成功）。  
- [x] 板测：组网后 `zbcmd` 路径可用（用户确认符合预期）。

### M4 — 文档与合入

- [x] 更新 `projects/factory/README.md`：TOOL/UUT 双机步骤、命令表、与量产界限。  
- [x] 写明量产目标烧录形态：整包 hex = **bootloader + app + factory**（现阶段仍 factory 现有烧录）。  
- [x] `zigbee_ed` / `zigbee_router` README 链接本计划（量产禁止本厂测 Coord）。  
- [x] `decisions.md`：厂测单镜像允许临时 TOOL；量产禁止；禁止厂测双产物；不新建 zigbee_factory。  
- [ ] （可选）`firmware/x.y.z/` 归档**一份** factory 厂测包说明。  
- [x] 本计划文档同步到 R1 / M0–M3 落地状态。

### 后续（不进本期）

- [ ] 真 ED-UUT（另库 / 双角色链接再议）。  
- [ ] 量产整包 hex（bootloader + app + factory）产线流程与本厂测通道对接。  
- [ ] 多白名单 / 自适应信道（明确不做除非新开计划）。

## 实施文件地图

| 路径 | 状态 | 说明 |
|------|------|------|
| `projects/factory/lib/cmd/*` | **已做** | `local_only` / remote capture；`zb`/`zbcmd` |
| `projects/factory/lib/zigbee/*` | **已做** | `zbft` TOOL/UUT + allow/join + zbcmd ZCL |
| `projects/factory/src/app.c` 等 | **已做** | `zbft_boot`；心跳 60s |
| `projects/factory/prj.conf` / `pm_static.yml` | **已做** | Zigbee + zboss@0x15E000 |
| `projects/factory/README.md` | **已做（M4）** | 双机步骤 + 命令表 + 界限 |
| `doc/plan_zigbee_factory_temp_coord.md` | **本文件** | 计划与门禁 |
| `doc/decisions.md` | **已做（M4）** | Factory Zigbee FTM 决策行 |
| `projects/zigbee_ed` / `zigbee_router` 业务代码 | **不改** | 量产边界；README 已链本计划 |
| `projects/zigbee_factory` | **不建 / 已撤** | 共用 factory |
| 量产整包 hex 工具/脚本 | **后续** | bootloader+app+factory |

## 验收

### 组网

1. [x] 两台 factory 烧录，上电见 `zbft: net wipe boot` / `settings kept`。  
2. [x] B：`zb uut` 直接回 **IEEE**。  
3. [x] A：`zb tool` → formed；未 allow 时不开窗（不业务入网）。  
4. [x] A：`zb join <B的IEEE>`；B → `uut joined`；A → `dut joined`。  
5. [ ] A 故意错 IEEE → `join deny`（代码已有；可再抽测）。  
6. [ ] （可选）`zb allow sn` 不符 → `sn warn`，其后 `zbcmd version` 仍可用。  
7. [x] A/B：`zb status` 同 ch/同 pan；allow/joined 可见。  
8. [ ] `zb ch 20` 后重跑组网（代码支持起栈前改 ch；可再抽测）。

### 厂测命令代理

9. [x] allow+joined 后：`zbcmd version|sn|log` 路径可用（板测通过；本地 sn ≠ 远端 sn）。  
10. [x] `zbcmd reboot` → B 复位可用（板测通过）。  
11. [x] 未 joined / 超时 → `zbcmd:ng`。  
12. [x] `help` 可见 `[local]`（zb / zbcmd / ccfg）。

### 清网 / Settings / allow

13. [ ] `reboot` 后网与 allow 不恢复；须重新 uut → tool → join（建议抽测）。  
14. [ ] 重启前后 `sn` 保持一致（S0 已覆盖逻辑；可再抽测）。  
15. [ ] 量产 `zigbee_ed` 抽测：无本厂测 Coord/命令通道（M4）。

### 负面

16. [ ] 两台同时 `zb tool`：状态可观察；只应一台 Coord（现场约定）。  
17. [x] 厂测仅一份产物（单 hex）。  
18. [x] 未 allow / 错 IEEE：deny 路径已实现。  
19. [ ] sn 不符仅 warn、不阻断（代码已有；可再抽测）。

## 风险与缓解

| 风险 | 缓解 |
|------|------|
| ZBOSS 角色编译期互斥 | R1：单 hex + COORDINATOR 库；UUT=Router；切换才 reboot |
| 清 NV 误清 sn/did | 只清 zboss 分区；上电 `settings kept` |
| 空中命令与本地语义分叉 | `cmd_process_line_remote` 同表；禁止第二套解析 |
| 厂测包流入市场当量产 | factory 槽位 / 版本串；量产 ed/router 不带本通道（M4 写清） |
| 默认 ch15 被占用 | `zb ch` 手动改；不做自适应 |
| 附近无关设备误入临时网 | allow IEEE + ANNCE 踢除 |
| 回调里做长命令 | EP handler 只 ingress；UUT 用 `k_work` 执行 |
| UUT 长退避导致「无反应」 | 固定 3s 重试；配对日志静默 |

## 下一步

**本期目标已闭环**：M0–M3 功能 + M4 主文档 + 双机 `zbcmd` 板测通过。

剩余可选：错 IEEE deny / `allow sn` warn / 改 ch 抽测；`firmware/` 归档；真 ED-UUT 与量产整包 hex → 见「后续」。

---

## 修订记录

| 日期 | 说明 |
|------|------|
| 2026-11-02 | 初稿：门禁 + 双机厂测临时 Coord + 重启清网；待确认 Q1–Q8 |
| 2026-11-02 | 关闭 Q1（删除）/Q2 单产物硬约束/Q3 全部可控厂测命令/Q4 Settings 重启保留；M0/M3/验收同步 |
| 2026-11-02 | 关闭 Q5 默认15可改、无自适应 / Q6 仅串口 / Q7 先 ED 后 Router / Q8 现阶段独立烧录、量产整包 hex |
| 2026-11-02 | 增补「实现逻辑图」：操作总览、双机时序、状态机、zbcmd 路径、清网语义 |
| 2026-11-02 | 流程图改为纯 text；入网增加 zb allow(IEEE) 身份门禁；串口/M2/验收同步 |
| 2026-11-02 | 锁定：IEEE 为主；sn 不符告警继续；白名单暂定容量=1 |
| 2026-11-02 | 实施顺序调整：当前第一步 = **S0 厂测 cmd** |
| 2026-11-02 | **工程落点改为共用 `projects/factory`**；撤销新建 zigbee_factory |
| 2026-11-02 | S0：factory `lib/cmd` 增加 local_only + `cmd_process_line_remote`；修 process 内嵌套锁 |
| 2026-11-02 | S0 补命令：mac/led/btn/ccfg + zb* 占位；mac 连续 hex；ccfg=UICR AP+ERASE |
| 2026-11-02 | ccfg 仅 APPROTECT（app）；zb 收束为 `zb`+`zbcmd` 两条 |
| 2026-11-02 | ccfg 策略：烧录可开/厂测手动/不自动重开；OTA 成功后可自动 lock |
| 2026-11-02 | M0：factory 接 Zigbee；机制 A 软复位；DUT=Router（单 hex）；zboss NV 借用 FLPR 头 |
| 2026-11-02 | 修：zboss 不可放 0x165000+（FLASH_SIZE=1428KiB）→ settings 缩 8KiB，zboss@0x15E000；cmd 栈 2KiB |
| 2026-11-02 | **R1 重锁定**：TOOL/UUT；上电 idle；首次原地起栈；切换才 reboot；废「每次 coord 必复位」 |
| 2026-11-02 | M1–M3 落地：allow/join 门禁、UUT 短重试、`zb join <ieee>` 最简、`zbcmd` 自定义 ZCL 代理；板测通过 |
| 2026-11-02 | 计划文档同步：操作总览/契约/里程碑/验收打勾；下一步 = M4 |
| 2026-11-02 | **M4**：factory README 双机步骤；decisions 决策行；ed/router README 量产边界链接 |
| 2026-11-02 | 板测闭环：`zbcmd sn` 远端值 ≠ 本地 sn；`zbcmd reboot` OK；本期验收通过 |
