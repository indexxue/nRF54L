# Zigbee 完整系统里有哪些设备

本文说明 Zigbee 网络里的**设备角色**、常见**应用设备类型**，以及本仓库联调拓扑如何对应。  
工程用法（烧录 / 按键 / 控灯）见 [`README.md`](README.md)。

---

## 1. 先分清两层概念

| 层 | 回答的问题 | 例子 |
|----|------------|------|
| **网络角色（Logical Device Type）** | 在网里干什么：建网？转发？休眠？ | Coordinator / Router / End Device |
| **应用类型（Device ID / Cluster）** | 业务是什么：灯？开关？传感器？ | On/Off Light、Occupancy Sensor |

同一台物理板可以是「Router + On/Off Light」，也可以是「End Device + 温湿度传感器」。  
**角色决定组网与功耗；Cluster 决定能不能控灯、报数据。**

---

## 2. 网络角色（每个 Zigbee 网必备的三类）

```text
                    ┌──────────────────┐
                    │  Coordinator     │  全网唯一：建网、分配地址、常作 Trust Center
                    │  （协调器）       │
                    └────────┬─────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
        ┌──────────┐   ┌──────────┐   ┌──────────┐
        │  Router  │   │  Router  │   │    ED    │
        │ （路由）  │───│ （路由）  │   │（终端）  │
        └────┬─────┘   └──────────┘   └──────────┘
             │
             ▼
        ┌──────────┐
        │    ED    │  可挂在 Router 或 Coordinator 下
        └──────────┘
```

### 2.1 Coordinator（协调器）

| 项 | 说明 |
|----|------|
| 数量 | **每个网络有且仅有 1 个** |
| 职责 | 创建网络（PAN ID / 信道）、分配短地址、开/关 permit join、常兼 Trust Center（入网鉴权） |
| 射频 | **常开**，不能当电池休眠节点 |
| 本仓库对应 | **ZigBridge-EM**（ZNP Host + E72 射频跑 Coordinator） |

没有 Coordinator，就没有可加入的 Zigbee 网。

### 2.2 Router（路由器）

| 项 | 说明 |
|----|------|
| 数量 | 0～多个（mesh） |
| 职责 | **转发**其它设备的帧、扩展覆盖、可作 ED 的 **parent** |
| 射频 | **常开**（要随时中继） |
| 入网 | 仍通过 Network Steering 加入已有 Coordinator 的网（不是再建一个网） |
| 本仓库 | `zigbee_demo` **当前不是** Router；芯片/SDK **可以**改成 Router（见需求：允许 Router，禁止本机做 Coordinator） |

Router **也能**带应用功能（例如本身就是一盏灯），中继与业务可并存。

### 2.3 End Device（终端设备，ED / ZED）

| 项 | 说明 |
|----|------|
| 数量 | 通常最多 |
| 职责 | 只处理自己的应用流量；**不转发**他人包 |
| 射频 | 可 **休眠**，靠 parent（Coordinator 或 Router）缓存下行 |
| 入网 | Steering 加入网络；掉线后找 parent / 再入网 |
| 本仓库对应 | **`zigbee_demo` 现状**（`CONFIG_ZIGBEE_ROLE_END_DEVICE`） |

电池供电、间歇上报类设备优先选 ED；需要扩网或常供电中继则选 Router。

### 2.4 角色对照速查

| | Coordinator | Router | End Device |
|--|:-----------:|:------:|:----------:|
| 建网 | ✅ 唯一 | ❌ | ❌ |
| 转发 / 扩网 | ✅（根） | ✅ | ❌ |
| 可深度休眠 | ❌ | ❌ | ✅ |
| 可挂应用（灯/传感器） | ✅ | ✅ | ✅ |
| 本联调谁扮演 | ZigBridge-EM | （可选，未做） | zigbee_demo |

---

## 3. 应用层常见设备类型（Device ID）

网络角色之上，ZCL 用 **Profile + Device ID + Endpoint + Cluster** 描述「这是什么产品」。  
Home Automation / Zigbee 3.0 常见例子：

| 类型（概念） | 典型 Cluster | 说明 |
|--------------|--------------|------|
| On/Off Light | OnOff `0x0006` Server | 被开关控制的灯 |
| On/Off Light Switch | OnOff Client | 发 On/Off 的开关 |
| Dimmer / Level Light | OnOff + Level Control | 调光灯 |
| Occupancy Sensor | Occupancy Sensing | 人体存在 |
| Temperature Sensor | Temperature Measurement | 温度 |
| IAS Zone | IAS Zone | 门磁 / 烟感等安防区 |
| Smart Plug | OnOff（+ 计量等） | 智能插座 |
| Thermostat | Thermostat 等 | 温控 |

本工程联调契约：

| 项 | 值 |
|----|-----|
| Endpoint | **1** |
| 应用 | **On/Off Light**（OnOff Server `0x0006`） |
| 网络角色 | **End Device** |
| 控法 | 网关空中 ZCL；不解析网关串口 ASCII |

官方样例对照（ncs-zigbee）：`light_bulb` ≈ Router + 灯；`light_switch` ≈ ED + 开关 Client。

---

## 4. 本仓库联调拓扑（最小完整系统）

最小可验收系统只需 **1× Coordinator + 1× ED（本灯）**：

```text
┌─────────────────────────────┐         Zigbee 空中          ┌─────────────────────────────┐
│  ZigBridge-EM               │◄────────────────────────────►│  nRF54L15 zigbee_demo       │
│  Coordinator + ZCL Client   │   join / ZCL OnOff → EP1     │  End Device + OnOff Server  │
│  CLI: znp join / znp light  │                              │  LED_G = 灯                 │
└─────────────────────────────┘                              └─────────────────────────────┘
```

若以后加 Router，典型形态：

```text
Coordinator (ZigBridge) ── Router（常电中继，可选兼灯）── End Device（远端 / 电池）
```

验证 Router 是否在中继：需要「远端 ED 只能听到 Router、听不到 Coordinator」的场景；仅控灯成功**不能**证明是 Router。

---

## 5. 和「网关 / ZNP / Host」的关系

| 名称 | 是什么 | 怎么形容更贴切 |
|------|--------|----------------|
| **Coordinator（协调器）** | Zigbee **网络角色**：建网、发短地址、permit join、常兼 Trust Center | 「这张 Zigbee 网的**根 / 建网者**」 |
| **Gateway（网关）** | **产品形态**：把 Zigbee 接到串口 CLI / 以太网 / MQTT / 云 | 「Zigbee 与外部系统之间的**桥**」 |
| **ZNP** | 射频侧跑 Zigbee 栈、Host 用串口命令控栈的常见形态 | 实现网关时的一种拆分方式 |
| **ZigBridge-EM** | Host（`znp_host`）+ 射频 Coordinator | **网关产品**；其射频侧扮演协调器 |

协调器 **≠** 网关：网关里通常**带有**协调器；没有对外上行、只建本地网的 Coordinator，严格说也不叫网关。  
子设备（本 `zigbee_demo`）**不需要**实现 ZNP；只做标准 Zigbee 设备即可。

---

## 6. 组网与 parent 行为（FAQ）

先分清两件事：

| 概念 | 含义 |
|------|------|
| **在网** | 仍持有这张网的密钥 / 身份（没 leave、没清 NV） |
| **可达** | 到协调器（网关）有完整射频路径，能被控、能上报 |

Router 掉线或 ED 挪远：多数是 **短暂不可达 → 换 parent / 绕路**，不是立刻退网。  
只有 leave、清 NV、换网，才是真离网。

### 6.1 ED 能否经 Router 入网（不必射频直连协调器）？

**可以（射频 / 父子关系）；网络仍属协调器。**

1. 协调器建网  
2. Router 先 Steering 入网  
3. 协调器打开 permit join（Router 也会跟着开窗）  
4. 新 ED Steering → 可只「够得着」某台 Router，以它为 **parent** 关联  
5. 鉴权 / Trust Center 仍在协调器；经 mesh 完成入网  

ED **不是**加入「Router 自己的网」。没有协调器、或未开 join，光有 Router 也不能让新设备第一次入网。

### 6.2 ED 同时在协调器和 Router 范围内，怎么选 parent？

**没有「必须挂协调器」的硬规则。** ED 在 Steering 时发信标请求，所有**已开 permit join**且能响应的节点（协调器 + 各 Router）都是候选 parent；栈按实现挑选一个，常见倾向：

| 因素 | 典型影响 |
|------|----------|
| **链路质量（LQI / RSSI）** | 信号更好的更优先（往往是决定性因素） |
| **网络深度 / 跳数** | 有的栈更偏向更浅的节点（协调器深度更浅） |
| **对方是否允许新子设备** | child 表满、未开 join → 不会被选 |
| **谁先回应** | 实现相关；近距离时协调器与 Router 都可能被选中 |

因此：

```text
同一房间里 Coord 与 Router 都开着 join：
  → ED 可能挂在 Coord 下，也可能挂在 Router 下
  → 两种都算「已入同一张网」；业务上都能被网关控灯
  → 应用层一般不需要、也无法指定「必须挂某某」
```

联调若只有 Coord + 一台 ED、且无 Router，parent 自然就是协调器。  
要验证「经 Router 入网」，应让 ED **够不到** Coord、只够得到已入网的 Router。

### 6.3 ED 从 Route-A 范围挪到 Route-B 范围？

**通常换 parent（reparent / rejoin），仍在原网，不是离网。**

```text
原先：  Coord ── Route-A ← ED
后来：  Coord ── Route-B ← ED   （同一 PAN）
```

ED 发现原 parent 丢失 → 在同网内找新 parent → 挂到 B。  
换 parent 期间可能短暂控不了；这与「BTN2 清网再第一次入网」不是一回事。  
若附近没有任何同网 Router/协调器 → 会长时间失联（身份可能还在，只是不可达）。

### 6.4 删掉某台 Router，ED 还在别的 Router 或协调器范围内？

| 情况 | 结果 |
|------|------|
| ED 的 parent **就是**被删的那台 | 失联 → 找新 parent；找得到则仍在网且又能控 |
| ED 本来就挂在别的 Router / 协调器上 | 基本无感 |

删 Router **≠** 踢掉全网 ED；只影响以其为 parent 的子设备，以及必须经它绕路的路径。

### 6.5 Router 之间能否串联？中间断一个会怎样？

Router **之间本来就会**建路由（mesh），不必都直连协调器。

有绕路时：

```text
Coord ── R1 ── R2 ── R3
         └─── R4 ──┘     R2 挂了 → 可走 R4，R3 及其下 ED 往往仍可控
```

真·单线、无备份时：

```text
Coord ── R1 ── R2 ── R3←ED
              ↑ 只断这里
```

| 节点 | 还「在网」吗 | 网关还能控吗 |
|------|--------------|--------------|
| R1 | 是 | 是 |
| R2 | 设备已不在 | — |
| R3 及挂在其下的 ED | 往往仍自认在网 | **不能**（孤岛，到不了协调器） |
| 若 R3 其实也能直接听到 Coord / R1 | 栈会重算路由 | 可能稍后恢复 |

要点：

- 中间断一个 **≠** 整网解散；断点靠前且仍连通的那段都还在。  
- 断点后面若无第二条射频路径 → **在网但不可达**，直到拓扑恢复或 ED/Router rejoin 到能通的路径。  
- 量产应避免过长单链；多放市电 Router，形成网状更扛断点。  
- ED 能不能控，看的是：**它的 parent 到协调器是否还有路**。

### 6.6 产品举例（谁常当协调器 / 路由 / ED）

| 角色 | 常见产品 |
|------|----------|
| 协调器（或网关内的协调器） | Hue Bridge、IKEA Dirigera、Aqara/米家多模网关、SmartThings Hub、USB 棒+Zigbee2MQTT/ZHA、**ZigBridge-EM** |
| 路由 | 市电球泡、Zigbee 插座、零火墙开、Range Extender、nRF 官方 `light_bulb` 样例 |
| 终端 ED | 门磁/温湿度/PIR、电池遥控、**本 `zigbee_demo`（现状）** |

市电常开适合 Router；电池设备适合 ED；家里唯一的 Hub/棒才是 Coordinator。

---

## 7. 选型建议（本项目语境）

| 目标 | 建议角色 |
|------|----------|
| 联调入网 + `znp light` 控灯 | **ED**（当前镜像） |
| 扩覆盖、给远处 ED 当 parent | **Router**（另开实验；改 ROLE + 内存配置，清 NV 再入网） |
| 建网 / permit join / 设备表 | **只在网关 Coordinator**；nRF 侧需求禁止本机做 Coordinator |

---

## 8. 相关文档

| 文档 | 内容 |
|------|------|
| [`README.md`](README.md) | 本工程烧录、按键、EP/掩码 |
| [`../../doc/req_nrf54_zigbee_end_device.md`](../../doc/req_nrf54_zigbee_end_device.md) | 正式需求 / ZCL 契约 |
| [`../../doc/zigbee_demo_handoff_to_zigbridge.md`](../../doc/zigbee_demo_handoff_to_zigbridge.md) | 与 ZigBridge 联调交接 |
| [`../../doc/zigbee_demo_pairing_control.md`](../../doc/zigbee_demo_pairing_control.md) | 配网窗 / Steering / leave |
