# 交接：nRF54L15 `zigbee_router` 入网后无法稳定留网 → 请 ZigBridge / 协调器侧会诊

> **给谁看**：ZigBridge-EM / ZNP 协调器同事  
> **子设备工程**：`projects/zigbee_router`（Router + EP1 OnOff Server）  
> **对照工程**：`projects/zigbee_ed`（同一硬件、同一 OnOff 契约；**ED 可入网且可被 `znp light` 控制**）  
> **日期**：2026-11-01  
> **请协调器侧判断**：TCLK / Transport Key / Router 入网策略是否正常；子设备侧暂缓再改代码，等会诊结论。

---

## 1. 一句话现象

nRF **Router** 镜像在 `znp join on` 窗口内能被协调器看到 `join(tc)`（IEEE 一致），但 **约 10s 内必 leave**，应用侧收不到 **`STEERING` 成功（sig=10, st=0）**，因此 **无法稳定在网，协调器 `znp light` 控不了 Router**。  
同板刷 **ED** 镜像时，入网与控灯正常。

---

## 2. 拓扑与契约（未改）

```text
ZigBridge-EM（Coordinator / ZNP）  ←—— Zigbee ——→  nRF54L15 zigbee_router
  znp join / znp light …                         EP1 · OnOff 0x0006 Server
                                                 信道扫描掩码 11/15/20/25
                                                 BTN1 = Steering；BTN2 双击 leave；
                                                 BTN2 长按 3s = 擦 zboss_nvram + 复位
```

| 项 | 值 |
|----|-----|
| 角色 | **Router**（`CONFIG_ZIGBEE_ROLE_ROUTER`） |
| Endpoint | **1** |
| Cluster | OnOff **`0x0006`** Server |
| 扫描掩码 | `0x02108800`（11/15/20/25） |
| 子设备不解析 | 网关 USART3 ASCII（只认空中 ZCL） |

---

## 3. IEEE / NWK 对拍（已确认是同一台设备）

| 侧 | 表示 |
|----|------|
| 协调器日志 | `ieee=A06DDCEACD36CEF4`，`nwk=0xE944`（多次复现） |
| 子设备日志 | `ieee=F4CE36CDEADC6DA0`，`nwk=0xe944` |

二者为 **同一 EUI64 的端序互逆打印**，不是两台设备。

---

## 4. 典型时间线（多轮复现）

### 4.1 较早一轮（子设备曾过早报 `joined`）

| 相对时间 | 协调器 | Router 子设备 |
|----------|--------|----------------|
| T+0 | permit join / 按键开窗 | BTN1 → `steering` |
| ~T+2～3s | `join(tc) nwk=0xE944 ieee=A06D…` | （曾）`ZB_JOINED()==1`，应用误报 `joined` |
| ~T+10～13s | — | `sig=3` **LEAVE** → `sig=10 st=-1` **STEERING 失败** → idle |

间隔约 **10s** 的 leave，符合 Nordic 文档中 **TCLK（Trust Center Link Key）交换失败** 的常见时序：先关联 / 半入网，密钥谈不成再 leave，随后 BDB Steering 失败。

### 4.2 最新一轮（已改为「只认 STEERING OK」之后，2026-11-01 11:15）

子设备串口（摘要）：

```text
BTN1 → zigbee: steering (mask=0x02108800 ep=1)
…（约 13s）
zigbee: sig=3 st=0 joined=0 win=1
zigbee: leave during steer — keep window, retry
zigbee: sig=10 st=-1 joined=0 win=1
```

要点：

- **配网窗仍开着（win=1）** 就收到 **LEAVE**，随后 **Steering 失败（st=-1）**  
- **没有出现** `sig=10 st=0`（Steering 成功）  
- 因此应用层 **不会** 再报「真正入网完成」，也不会认为可长期控灯  

信号编号对照（ZBOSS）：

| sig | 含义 |
|-----|------|
| 1 | `ZB_ZDO_SIGNAL_SKIP_STARTUP` |
| 3 | `ZB_ZDO_SIGNAL_LEAVE` |
| 5 | `ZB_BDB_SIGNAL_DEVICE_FIRST_START` |
| 10 | `ZB_BDB_SIGNAL_STEERING`（**st=0 才算配网成功含 TCLK**） |
| 22 | `ZB_COMMON_SIGNAL_CAN_SLEEP`（可忽略刷屏） |

---

## 5. 子设备侧已排除 / 已修正（供参考）

| 项 | 结论 |
|----|------|
| 不是「控灯 Cluster/EP 写错」 | ED 同契约可 `znp light`；Router 问题出在 **留网 / 配网收尾** |
| 不是单纯「扫不到网」 | 协调器有 `join(tc)`，IEEE 对得上 |
| ED↔Router 脏 NV | 已提供 BTN2 长按 3s 擦 `zboss_nvram`；仍复现 leave |
| 过早用 `ZB_JOINED()` 当成功 | Nordic 说明：NWK 层可能已 joined，但 **device_annce / TCLK 未完成**；过早 `join_commissioning_set_active(false)` / `rejoin_abort` 会 **掐断密钥交换**。子设备已改为：**仅 `STEERING` 且 `st=0` 才报 joined** |
| 当前仍失败 | 在「不再过早掐 commissioning」之后，仍出现 **窗内 LEAVE + Steering st=-1** → 更指向 **空中 TCLK / 协调器对 Router 的处理**，而非单纯 UI 状态机误报 |

---

## 6. 请协调器侧重点判断的问题

1. **ZNP / 协调器对 Router 入网是否完整支持 TCLK（Transport Key / Request Key）？**  
   ED 能完成、Router 约 10s leave，是否与设备类型（Router vs Sleepy ED）或 ZDO 能力位有关？

2. **`join(tc)` 日志是否表示「仅关联成功」还是「含 TCLK 已完成」？**  
   若仅为 TC 侧看到关联，请对照其后是否有 **Transport Key / Update Device / Device Annce** 成功日志；Router 场景是否缺步骤或超时。

3. **permit join 窗口内，对 Router 是否有特殊限制？**  
   （例如只允许 ED、child 表策略、TC 策略、安装码、仅集中式安全等。）

4. **请抓一轮 Router 入网的协调器完整日志**（与子设备 11:15 左右同场更佳），包含：  
   - `znp join on`  
   - `join(tc)` 前后 ZDO/AF/安全相关  
   - 约 10s 后是否有 leave / remove / key failure  

5. **对照实验**：同一协调器、同一信道，仅换 nRF **ED** 固件 → 应仍可控灯；再换 **Router** → 是否稳定复现 leave。若是，请在网关侧标为 **Router 入网/TCLK 路径问题** 优先查。

---

## 7. 子设备侧当前策略（暂不再改代码）

- 显式配网：BTN1 开 60s 窗 + Network Steering；窗内 leave / Steering 失败会重试  
- 真正成功条件：**仅** `ZB_BDB_SIGNAL_STEERING` 且 status OK  
- 清脏网：BTN2 长按 3s 擦 `zboss_nvram` 并复位  
- **等待协调器会诊结论后再决定**：是改网关 TCLK/Router 策略，还是子设备再补调试（例如更细的 APS/安全 trace）

---

## 8. 相关路径

| 路径 | 说明 |
|------|------|
| 本文件 | Router 入网 leave 问题交接 |
| `projects/zigbee_router/README.md` | Router 工程用法 |
| `projects/zigbee_ed/` | 可对照的 ED 成功路径 |
| `doc/zigbee_demo_handoff_to_zigbridge.md` | 原 ED 交付 / 控灯契约 |
| `doc/req_nrf54_zigbee_end_device.md` | ZCL 契约（允许 Router） |

---

## 9. 请协调器回传

1. 对「Router join(tc) 后 ~10s leave」的判断（TCLK？策略？ZNP 版本？）  
2. 同场协调器原始日志片段（含 join 前后）  
3. 建议子设备下一步：继续等网关修复 / 或需要子设备开某类 trace / 或改角色策略  

**子设备承接**：nRF54L15 `zigbee_router`（暂缓改码，等会诊）  
**网关承接**：ZigBridge-EM ZNP 协调器入网与 TCLK 路径排查  
