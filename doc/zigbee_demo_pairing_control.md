# zigbee_demo 配网控制逻辑

> **工程**：`projects/zigbee_ed`（原 `zigbee_demo`；Router 工程 `zigbee_router` 配网 UX 相同）  
> **核心实现**：`lib/zigbee/zigbee_app.c`（协议 / 状态）；`src/app.c` 仅消费事件做灯效与日志  
> **角色**：Zigbee End Device；多信道扫描 **11/15/20/25**（`CONFIG_ZIGBEE_CHANNEL_MASK=0x02108800`，对齐网关 M5）  
> **相关**：[`req_nrf54_zigbee_end_device.md`](req_nrf54_zigbee_end_device.md)、[`zigbee_demo_handoff_to_zigbridge.md`](zigbee_demo_handoff_to_zigbridge.md)、[`zigbee_rejoin_contract.md`](zigbee_rejoin_contract.md)（掉电同网 rejoin Must）

本文描述**配网状态机与行为契约**。触发入网 / 清网的**物理按键可能改动**，正文用逻辑动作称呼；按键映射见文末宏说明。

---

## 1. 设计原则

| 原则 | 说明 |
|------|------|
| 显式入网 | 仅调用 `zigbee_app_steer_request()` 才开配网窗并发起 Network Steering |
| 显式清网 | 仅调用 `zigbee_app_leave_request()` 才 leave / 本地 BDB 复位 |
| **rejoin 分界** | 与 [`zigbee_rejoin_contract.md`](zigbee_rejoin_contract.md) **§2.3** 一致（见下） |
| 禁止未入网自动搜网 | **未入网**（出厂 / leave / 清 NV）：抑制 Nordic 默认自动 steering / 未入网搜网 |
| 已入网须同网 rejoin | **已入网身份仍在**（含协调器整机掉电后同网）：Must 同网 rejoin / 父节点恢复至应用可达；**不得** abort 掉同网恢复 |
| 配网有时限 | 入网请求开启 **`ZIGBEE_APP_STEER_WINDOW_MS`（当前 60s）** 窗；窗内失败重试，超时才 `FAILED` |
| 多信道扫描 | `CHANNEL_SELECTION_MODE_MULTI` + 掩码含 **11/15/20/25**；入网后跟协调器实际信道（NV 由 ZBOSS 保持） |
| 回调只入队 | ZBOSS 信号 / ZCL 回调只 `msgq` 上报；刷屏级 UI 在 `app_run` 侧消费 |

```text
未入网  →  不自动搜网；仅显式 steer_request
已入网  →  必须能同网 rejoin（协调器重启后仍回到同一小区）
断代后  →  先 leave/清 NV，再显式 steering
```

Nordic `zigbee_default_signal_handler()` 在 leave / Steering 失败后会自动 rejoin。  
**仅在未入网路径**按需 `zigbee_network_rejoin_abort()`，避免「清网后自行搜网」或「未触发入网却自己搜网」。  
**已入网掉线 / 协调器重启**：本侧 `s_same_net_rejoin_active` + BDB steering 同网 rejoin（`NO_ACTIVE_LINKS` / `DEVICE_REBOOT` 失败）；掐掉 Nordic 未入网退避以免双开，由本侧指数退避重试（2s…64s）。

---

## 2. 逻辑动作与应用状态

### 2.1 逻辑动作（与按键解耦）

| 逻辑动作 | API | 行为摘要 |
|----------|-----|----------|
| **入网请求** | `zigbee_app_steer_request()` | 开配网窗 → 上报 `STEERING` → `bdb_start_top_level_commissioning(STEERING)` |
| **清网请求** | `zigbee_app_leave_request()` | 关窗 → `rejoin_abort` → `zb_bdb_reset_via_local_action(0)` |

### 2.2 应用状态（`zigbee_app_state_t`）

| 状态 | 含义 | 串口（示意） | LED 场景（当前 demo） |
|------|------|--------------|------------------------|
| `IDLE` | 未在网 / 已 leave，等待入网请求 | `zigbee: idle / left` | TRIGGER |
| `STEERING` | 配网窗打开，正在 / 即将 Network Steering | `zigbee: steering` | PAIRING |
| `REJOINING` | 已入网身份仍在，同网 rejoin 中（§2.3） | `zigbee: rejoining (same-net)` / `rejoin start|fail|ok` | PAIRING |
| `JOINED` | 已入网，可收 ZCL | `zigbee: joined nwk=… ieee=…` | SUCCESS |
| `FAILED` | 配网窗超时，或窗外未入网 Steering 失败 | `zigbee: failed` | ERROR |

另有独立事件 `ZIGBEE_APP_EVT_LIGHT`（OnOff），**不改变**上表网络状态；控灯时可能改写 LED，与网络灯效存在抢灯（见 §7）。

配网窗时长：`ZIGBEE_APP_STEER_WINDOW_MS`（`zigbee_app.c`）。

---

## 3. 状态机（丰富版）

### 3.0 先用人话看懂（读图前）

把设备想成「要进一栋楼的访客」，网关是「前台」：

1. **平时（IDLE）**  
   设备在门口闲着，**不会自己乱敲门**。这样省电，也不会在没人准备接待时乱连别人家的网。

2. **你发出「入网请求」**  
   等于说：「我现在要进门了，请帮我找前台。」设备进入 **STEERING（搜网中）**，灯会提示「正在配网」。

3. **为什么要开 60 秒「配网窗」？**  
   前台开门（网关 permit join）和设备敲门往往对不齐时间：可能你先按了键、网关晚两秒才开门，或中间一次没敲上。  
   若「敲一次没进就立刻失败」，体验很差。所以约定：**这 60 秒内可以反复再敲**；只有 60 秒都进不去，才报 **FAILED（失败）**。  
   窗还在时，中间某次没进门 → **继续搜，不要立刻亮红灯失败**。

4. **进门成功（JOINED）**  
   拿到「门卡」（网络地址），以后控灯等命令都认这张卡。断电再开机，卡还在 → 应直接回到 JOINED，不必再配一次。

5. **你发出「清网请求」**  
   等于「退卡、离开这栋楼」。设备回到 IDLE，**并且不要自动再去敲门**——否则你会看到「刚退出，过几秒又失败闪灯」，那是栈默认「离开后再搜网」在捣乱；我们故意关掉这种自动行为，等你下次明确再入网。

6. **上电三种结果**  
   - 以前配过且卡还在 → 直接 JOINED  
   - 从没配过 / 已退卡 → IDLE，等你下指令  
   - 启动异常 → FAILED（少见）

7. **为什么要画状态机？**  
   配网不是「按一下就成功」的一条直线，而是「闲着 → 限时搜网 → 成功或失败 → 可退网再来」。  
   把这些规矩写清楚，联调才知道：什么样的串口日志算正常，什么样的「秒失败」反而是旧逻辑/错误预期。

**一句话**：人控制「开始配」和「退网」；60 秒内允许失败重试；平时和退网后都不准自己偷偷配网。

下面是同一套规则的图示与表格。

### 3.1 总览

```text
                         ┌──────────────────────────────────────────┐
                         │            POWER-ON / RESET              │
                         └───────────────────┬──────────────────────┘
                                             │
                    ┌────────────────────────┼────────────────────────┐
                    │ DEVICE_* 已在网        │ 成功但未入网           │ status 失败
                    ▼                        ▼                        ▼
               ┌─────────┐              ┌─────────┐              ┌─────────┐
               │ JOINED  │              │  IDLE   │              │ FAILED  │
               └────┬────┘              └────┬────┘              └────┬────┘
                    │                        │                        │
                    │                        │ 入网请求               │ 入网请求
                    │                        │ (开窗)                 │ (开窗)
                    │                        ▼                        │
                    │                   ┌─────────┐◄──────────────────┘
                    │                   │STEERING │
                    │                   │ + 60s窗 │
                    │                   └─┬───┬─┬─┘
                    │         窗内失败重试│   │ │
                    │      (约0.5~1s) ───┘   │ │
                    │                        │ │
                    │         入网成功       │ │ 窗超时 / 窗外失败
                    │                        ▼ ▼
                    │                   JOINED / FAILED
                    │                        │
                    │         清网请求       │
                    └────────────────────────┤
                                             ▼
                                          IDLE
                                    (abort 自动 rejoin)
```

### 3.2 转移表

| 当前 | 触发 | 下一状态 | 附带动作 |
|------|------|----------|----------|
| （任意上电） | `DEVICE_FIRST_START` / `REBOOT` 且已 `ZB_JOINED()` | `JOINED` | 关窗；上报 IEEE/NWK |
| （任意上电） | 同上且成功但未入网 | `IDLE` | **abort** 默认自动搜网 |
| （任意上电） | 同上且 status≠OK | `FAILED` | **abort** 默认自动搜网 |
| `IDLE` / `FAILED` / `JOINED` | **入网请求** | `STEERING` | 开/刷新 60s 窗；发起 Steering |
| `STEERING` | `STEERING`/`CANCELLED` 成功且已入网 | `JOINED` | 关窗 |
| `STEERING` | 同上失败，且窗仍有效 | **保持** `STEERING` | abort 默认 rejoin；约 500ms 后再 `do_steer` |
| `STEERING` | `bdb_start_…` 被拒，且窗仍有效 | **保持** `STEERING` | 约 1s 后再 `do_steer` |
| `STEERING` | 60s 窗超时 | `FAILED` | 关窗标志；`rejoin_abort` |
| `STEERING` | Steering 失败且窗已关 | `FAILED` | `rejoin_abort` |
| 任意（含 `STEERING`） | **清网请求** | →（异步）`IDLE` | 关窗；abort；`bdb_reset_via_local_action`；`LEAVE` 后 abort |
| `JOINED` | 父节点丢失等栈行为 | *见 §7 缺口* | 当前未单独建模状态 |

### 3.3 配网窗内部时序

```text
入网请求
  │
  ├─► s_steer_window_active = 1
  ├─► k_work_reschedule(60s) ──超时──► FAILED + rejoin_abort
  ├─► 上报 STEERING（UI 保持 PAIRING，中间失败不改 ERROR）
  └─► do_steer:
        rejoin_abort
        bdb_start_top_level_commissioning(STEERING)
              │
              ▼
        ZB_BDB_SIGNAL_STEERING / STEERING_CANCELLED
              │
      ┌───────┴────────┐
      │ OK && JOINED   │ else
      ▼                ▼
   关窗→JOINED    窗有效？──是──► 0.5s 后再 do_steer
                      │
                      否──► FAILED
```

再次 **入网请求**：`k_work_reschedule` **刷新** 60s，相当于重新开窗。

### 3.4 清网时序

```text
清网请求
  │
  ├─► 关配网窗（打断 STEERING）
  ├─► zigbee_network_rejoin_abort()
  └─► zb_bdb_reset_via_local_action(0)
        │
        ▼
      ZB_ZDO_SIGNAL_LEAVE
        │
        ├─► 上报 IDLE
        ├─► default_signal_handler（其内可能 start_network_rejoin）
        └─► 再 zigbee_network_rejoin_abort()  → 停在 IDLE
```

### 3.5 与控灯的并行关系

```text
网络状态机          灯属性（OnOff）
IDLE/STEERING/…  ×  on_off_attr / LED_G
       │                    │
       │    JOINED 后       │
       └──── ZCL On/Off ────┘
```

- 网络事件与灯事件分 `kind` 入队；`JOINED` 后 SUCCESS 灯效与 `light: on/off` **会争用同一组 LED**（demo 已知粗糙点）。
- leave → `IDLE` 时应用侧会清 `s_light_on`，不保证与 ZCL 属性再同步一次。

---

## 4. 上电与掉电保网

| 信号 | 条件 | 行为 |
|------|------|------|
| `DEVICE_REBOOT` | OK 且 `ZB_JOINED` | → `JOINED`（栈已用 NV 恢复） |
| `DEVICE_REBOOT` | 失败且 NFN | → `REJOINING` + `rejoin start reason=reboot_fail`；本侧同网 steering 退避 |
| `DEVICE_FIRST_START` / `DEVICE_REBOOT` | 成功但**未入网** | → `IDLE`，**abort** 自动搜网 |
| 同上 | 失败且 factory new | → `FAILED`；abort 自动搜网 |
| `NO_ACTIVE_LINKS_LEFT` | NFN（协调器掉电等） | → `REJOINING` + `rejoin start reason=parent_unreachable` |

网络凭证落在 ZBOSS NVRAM（`zboss_nvram` / `pm_static.yml`）。  
**已入网设备重启 / 协调器同网恢复**：保留 NV，不要求再按入网键；串口应见 `rejoin …` 直至 `rejoin ok` / `joined`，不能假设「本机曾 JOINED = 网关已能控灯」。  

> **实现状态（2026-11-01）**：§2.3 已在 `zigbee_ed` / `zigbee_router` 落地；与「禁止未入网自动搜网」分界见契约。

---

## 5. 网关侧配合（联调）

```text
网关 permit join 打开（时长建议 ≥ 子设备配网窗）
    → 子设备 入网请求（STEERING ≤ 窗长）
    → 对照 IEEE / NWK
    → 关 join 窗 → ZCL OnOff 控灯
    → 可选 清网请求 → 再入网
```

子设备窗与协调器 **permit join** 是两套时钟：协调器窗先关时，子设备仍会在窗内重试直到超时 `FAILED`。

---

## 6. 串口日志（逻辑向）

| 日志 | 含义 |
|------|------|
| `zigbee: steer request …` | 进入入网请求 |
| `zigbee: steer window N ms` | 配网窗已开 / 刷新 |
| `zigbee: steering` | UI → `STEERING` |
| `zigbee: joined nwk=… ieee=…` | 入网成功 |
| `zigbee: steer window timeout` | 窗到时 |
| `zigbee: failed` | 超时或窗外失败 |
| `zigbee: leave / factory reset network` | 执行本地清网 |
| `zigbee: idle / left` | 已离网并停住 |
| `light: on` / `light: off` | ZCL OnOff（与网络态独立） |

窗内中间 Steering 失败**不应**连续刷 `failed`。

---

## 7. 相对真正产品的缺口

下列为 **demo 已够联调、量产 / 家用产品仍缺或需改** 的部分，便于排期；不表示本期必须做完。

### 7.1 组网与信道

| 缺口 | 现状 | 产品期望 |
|------|------|----------|
| 扫描掩码仍固定四信道 | 已开 MULTI：`0x02108800`（11/15/20/25），对齐网关默认掩码 | 可按地区/SKU 配掩码；App 可展示「正在扫描的信道」 |
| 入网后工作信道 | 跟协调器；依赖 ZBOSS NV（未在应用层单独打印当前 ch） | 串口/App 可读当前工作信道，便于售后对照 |
| 无 Install Code / 增强入网 UX | 开放入网即可 | 按产品安全策略：install code、二维码、触控配网等 |
| 无「配网中取消」独立动作 | 仅能清网或等超时 | 可选：短按取消仅关窗、不擦 NV（若已在网） |
| 协调器踢设备 / 远程 leave | 仅本地 `bdb_reset` | 产品策略：是否响应 Mgmt Leave、是否自动允许再入 |

### 7.2 在网可靠性

| 缺口 | 现状 | 产品期望 |
|------|------|----------|
| 父节点丢失 / 间接 leave | 已：`REJOINING` + 同网 rejoin 指数退避（2s…64s）；leave 后停 | 产品可 Kconfig 化超时 / App 提示；是否最终要求用户再开配网窗 |
| Sleepy ED / 功耗 | 未按电池标签优化 | `rx_on_when_idle`、轮询周期、配网期抬高占空比 |
| 配网窗与重试参数 | 60s / 0.5s / 1s 写死宏 | 可 Kconfig；与 App「正在添加设备」倒计时对齐 |

### 7.3 人机与指示

| 缺口 | 现状 | 产品期望 |
|------|------|----------|
| 按键方案易变 | 见文末；逻辑已与键位解耦不足（日志仍写 BTN） | UI 层只调 `steer_request` / `leave_request`；键位表单列 |
| 网络灯效 vs 业务灯 | SUCCESS 与 OnOff 抢 LED | 业务输出与状态指示分离（双色 / 闪码 / 屏） |
| `JOINED` 灯效常驻 | SUCCESS 场景一直跑 | 入网成功短暂提示后熄灭或交还业务灯 |
| 无语音/App 反馈 | 仅串口 + LED | App 显示搜网中 / 成功 / 失败原因码 |

### 7.4 安全、证书与生命周期

| 缺口 | 现状 | 产品期望 |
|------|------|----------|
| 量产证书 / Zigbee 认证路径 | 联调镜像 | 按目标市场认证集 |
| 出厂 / 老化清网 | 仅用户清网 API | 产线夹具命令、防误触（长按 N 秒） |
| OTA | 无 | 与分区 / MCUBoot 策略对齐（见 `flash_layout` 类文档） |
| 唯一标识与包装 | 串口打 IEEE | 包装条码 / App 扫码绑定与 IEEE 一致 |

### 7.5 协议与功能面

| 缺口 | 现状 | 产品期望 |
|------|------|----------|
| 仅 OnOff Server | EP1 `0x0006` | Identify、Basic 字符串、Level、场景等按品类 |
| 无 MQTT / 云 | 空中只到网关 | 云侧走网关；子设备仍只认 ZCL（需求已定，但产品流程未在本 demo 闭环） |
| BLE+Zigbee 共存 | 独立工程，未共存 | 若单芯片双栈，需另开需求（射频时间片、配网优先级） |

### 7.6 工程与观测

| 缺口 | 现状 | 产品期望 |
|------|------|----------|
| 失败原因过粗 | 统一 `failed` | 区分：超时、无网络、鉴权失败、忙拒绝等 |
| 自动化测试 | 手工按键 + 网关 CLI | 产测夹具触发 steer/leave；日志可机读 |
| 文档 / 键位 | 多处写死 BTN1/2 | **以 API + 文末宏为准**，避免正文绑死键位 |

---

## 8. 代码入口速查

| 项 | 位置 |
|----|------|
| 配网窗时长 | `ZIGBEE_APP_STEER_WINDOW_MS` |
| 开窗 / 关窗 / 超时 | `zigbee_app_steer_window_*` / `zigbee_app_do_steer_timeout` |
| 发起 Steering | `zigbee_app_do_steer` |
| 入网 / 清网 API | `zigbee_app_steer_request` / `zigbee_app_leave_request` |
| 信号、abort、窗内重试 | `zboss_signal_handler` |
| 状态 → 灯效 / 日志 | `src/app.c`（`app_zb_led_apply` / `app_zb_log_net`） |

---

## 9. 按键映射（易变 · 仅此节维护）

> 物理键位、单击/双击/长按**可能随时改**。业务只认上一节 API；改键时只改 `app.c`（及下表），**不必改状态机正文**。

| 逻辑动作 | 当前硬件映射（demo） | 代码锚点 |
|----------|----------------------|----------|
| 入网请求 | BTN1（`BTN_ID_CONFIRM`）**短按** `BTN_EVENT_SINGLE_CLICK` | `app_handle_button` → `zigbee_app_steer_request()` |
| 清网请求 | BTN2（`BTN_ID_BACK`）**双击** `BTN_EVENT_DOUBLE_CLICK` | 同上 → `zigbee_app_leave_request()` |

相关宏 / 标识（以板级与 `button.h` 为准，名称可能随板型变）：

```text
BTN_ID_CONFIRM / BTN_ID_BACK
BTN_EVENT_SINGLE_CLICK / BTN_EVENT_DOUBLE_CLICK
ZIGBEE_APP_STEER_WINDOW_MS                 # 配网窗，当前 60000
ZIGBEE_APP_ENDPOINT                        # 1
CONFIG_ZIGBEE_CHANNEL_SELECTION_MODE_MULTI # 多信道扫描
CONFIG_ZIGBEE_CHANNEL_MASK                 # 0x02108800 → ch 11/15/20/25
```

引脚见 `projects/zigbee_ed/board/app.overlay` 的 `zephyr,user`（`btn1-gpios` / `btn2-gpios`）。
