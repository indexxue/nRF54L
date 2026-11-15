# Design: ink_panel 产品电源策略（USB-C / 锂电池）

**日期**：2026-11-15  
**状态**：已实现（板级代码合入 `npm1300_board`；板上插拔/Ship 待人工验收）  
**落点**：`projects/ink_panel/board/npm1300_board.*`（产品策略）；`cbb/npm1300` 仅能力 API  
**硬件依据**：
- `docs/superpowers/plans/schematic_wiring/02_power_npm1300.md`
- `docs/superpowers/plans/schematic_wiring/01_mcu_io_map.md`
- `docs/superpowers/specs/2026-11-15-npm1300-cbb-driver-design.md`

---

## 1. 目标与非目标

### 目标（本阶段 Must）

1. 量产供电路径：**Type-C VBUS** 和/或 **单节锂电池**，不依赖 J-Link 灌电。
2. 起机后 **BUCK1 一律软件设 3.0 V**（`VOUT` → `SWCTRL`），保证 SoC/NOR 运行电压。
3. **有 VBUS 且未软件禁止** → 按默认参数开充；拔掉 VBUS → 关充。
4. **轨门控**：触摸 LDO1 常开；墨水屏 LS2 仅刷屏窗口开。
5. **Ship**：仅显式命令进入（先禁充 → 关充 → `ship_enter`）；退出靠硬件 SHPHLD/按键冷启动。

### 非目标（本阶段）

- 厂测专用充电/电源口子
- 空闲超时自动进 Ship
- 低电 / UVLO 软件关机策略（靠芯片；以后再加）
- nPM1300 INT 热插拔（本阶段用周期轮询）
- Fuel gauge、写 Settings 持久化充电禁止位
- 把产品策略塞进 `cbb/npm1300`

---

## 2. 已确认决策

| 项 | 选择 |
|----|------|
| 充电策略 | **B 简化**：插 VBUS 自动充；可软件禁止；**无厂测口子** |
| BUCK1 | **起机一律** `3000 mV` + SWCTRL |
| LDO1 / LS2 | **A**：触摸常开；屏按需（MCU GPIO 优先） |
| Ship | **显式命令**；退出靠 SHPHLD / 用户键 |
| ICHG / VTERM | **300 mA / 4200 mV** |
| 充电禁止 | **RAM 标志**；掉电丢失 |
| VBUS 感知 | **1–2 s 周期读**状态；插→开充，拔→关充 |
| 低电 | **本阶段不做** |
| 策略代码落点 | `npm1300_board`；cbb 只提供寄存器能力 |

---

## 3. 电源树（产品视角）

```text
Type-C VBUS ──► nPM1300 SYSREG ──► VSYS
单节 VBAT   ↔  CHARGER          │
                                 ├─ BUCK1 ──► VDD_SoC（固件 3.0 V）
                                 ├─ LDO1  ──► VDD_Touch（TOUCH_LDO_EN = P0.03，常高）
                                 └─ LS2   ──► VDD_EPD  （EPD_PWR_EN  = P0.02，刷屏才高）
```

- 死电池 + 插 Type-C：外置 CC Rd（方案 A）可起 SYSREG → 固件抬 BUCK1。
- 仅电池、无 VBUS：靠 VBAT 供 VSYS；不起充。
- J-Link 单独灌电仅调试；量产验收不以该路径为准。

---

## 4. 运行时行为

### 4.1 起机序列（`npm1300_board_bringup` 扩展）

在线程上下文执行（可短暂 sleep；**禁止**放进 GATT/ISR）：

1. I2C ready → `npm1300_init`（probe-only）
2. `npm1300_buck_set_vout_mv(BUCK1, 3000)`
3. GPIO：`TOUCH_LDO_EN = 1`；`EPD_PWR_EN = 0`
4. 读 VBUS 在位；若在位且 `charge_inhibit == false`：
   - `charger_enable(false)` → `set_ichg_ma(300)` → `set_vterm_mv(4200)` → `charger_enable(true)`
5. 记下当前「期望充电态」，启动 1–2 s 周期 poll
6. 短日志一行（沿用/收敛现有 status 字段）

失败：返回错误码；调用方决定是否继续应用（本阶段：bring-up 失败则应用侧可见 log，不自动 Ship）。

### 4.2 VBUS 轮询

- **周期**：1–2 s（实现取固定值，如 `K_SECONDS(1)`）
- **上下文**：`k_work_delayable` 或等价工作线程；**非 ISR**
- **逻辑**：
  - `vbus_present && !charge_inhibit` → 确保已按 300 mA / 4.2 V 配置并使能（仅在状态边沿或「期望开但实际关」时写寄存器，避免每秒重配）
  - `!vbus_present || charge_inhibit` → `charger_enable(false)`
- **VBUS 判定**：读 `VBUSINSTATUS`（现板级已用 `0x0207`）；以 PS「VBUS detected」位为准（实现计划写清 mask）。本阶段 **不** 要求 cbb 新增 API；若为整洁可后续薄封装 `npm1300_vbus_is_present`，非本阶段硬依赖。

### 4.3 轨门控

| 轨 | 控制 | 策略 |
|----|------|------|
| LDO1 触摸 | I2C `ldsw`（LDO 3.0 V）+ `TOUCH_LDO_EN` (P0.03) | **TEMP 验证**：常开 |
| LDSW2 墨水屏 | I2C `ldsw`（**LDO 3.3 V**，非 LoadSW）+ `EPD_PWR_EN` (P0.02) | **TEMP 验证**：常开；功能完后再门控 / 评估改回 3.0 V 同域 |

- **实际使能以 I2C 为准**：nPM LDSW 默认关；仅拉 MCU EN 且未配 `LDSWnGPISEL` 时无输出（EVT 已验证）。
- MCU GPIO 同步驱动，预留日后 GPIO 映射；当前勿只依赖 GPIO。
- 刷屏调用方走 `npm1300_board_epd_power_set()`。

### 4.4 软件禁止充电与 Ship

```text
charge_inhibit (RAM, 默认 false)
        │
        ├─ true  → poll / bring-up 均不开充
        └─ false → 有 VBUS 则开充

npm1300_board_enter_ship():
  1. charge_inhibit = true
  2. charger_enable(false)
  3. EPD_PWR_EN = 0（可选：触摸也可关以省仓储漏电；本阶段建议关 EPD，触摸保持或关均可——默认关 EPD，触摸随 SoC 掉电）
  4. npm1300_ship_enter()
  5. 之后不应再依赖 I2C；MCU 随 VSYS 掉电或停振
```

退出 Ship：用户键 / SHPHLD 硬件唤醒 → 整机冷启动 → 重跑起机序列（`charge_inhibit` 复位为 false）。

显式 Ship 入口可由串口命令或后续 BLE CMD 调用；**本阶段至少提供 C API**；串口接线可在实现计划里列为可选验收项。

### 4.5 明确不做的异常路径

- 低电不主动 Ship / 不主动降频
- 不写 Flash/Settings
- 不使用 `NPM_INT_N`（P0.04）做本阶段充电状态机

---

## 5. API 落点（板级）

| API | 职责 |
|-----|------|
| `npm1300_board_bringup()` | 起机序列 + 启动 poll |
| `npm1300_board_poll_charge()` | 供 work 调用；或内部自挂 delayable |
| `npm1300_board_charge_inhibit_set(bool)` | RAM 禁止充电 |
| `npm1300_board_enter_ship(void)` | 禁充 → 关充 → Ship |
| `npm1300_board_epd_power_set(bool)` | `EPD_PWR_EN` |
| `npm1300_board_touch_power_set(bool)` | `TOUCH_LDO_EN` |
| `npm1300_board_log_status()` | 调试短日志（可保留） |

`cbb/npm1300`：**不**新增产品状态机；沿用现有 charger/buck/ship API。ICHG 变更顺序遵守驱动 README：禁用 → 设 ICHG/VTERM → 再使能。

---

## 6. 并发与硬约束

| 约束 | 要求 |
|------|------|
| 回调 / ISR | 只允许置标志或 `K_NO_WAIT` 入队；禁止 sleep / 刷屏 / 写 Settings / 长 I2C 策略 |
| I2C | 仅线程/work 上下文访问 `g_npm1300` |
| 刷屏 | 专用路径开关 LS2；不堵死 GATT 回调 |
| Flash | 本策略 **零** Settings 写入 |
| 引脚 | `P0.02` / `P0.03` 走 DT/overlay GPIO，不硬编码魔法口（若 overlay 尚无节点则补） |

---

## 7. 验收标准

| # | 场景 | 可观察 |
|---|------|--------|
| 1 | 仅电池上电 | bring-up 成功；BUCK1 日志 `vout` 对应 3.0 V；`chg` 关 |
| 2 | 仅 Type-C（可无电池） | 同上；VBUS 在位则开充（无电池时以芯片 status 为准，不崩溃） |
| 3 | 电池 + 插拔 Type-C | 插后 ≤2 s 内开充；拔后 ≤2 s 内关充 |
| 4 | `charge_inhibit` | 有 VBUS 也不开充 |
| 5 | 刷屏 | `EPD_PWR_EN` 窗口高，结束后低 |
| 6 | Ship | 调 `enter_ship` 后系统停；按键可再起并重跑起机 |
| 7 | 共存 | 轮询进行时串口日志 / 后续 PING 路径不被堵死（work 短持） |

调试手段：UART 日志；万用表量 `VDD_SoC` / `VDD_EPD`；nRF Connect 非本阶段必须。

---

## 8. 合入与风险

- **分支**：实验/功能分支合入前自测 1–3、5；Ship（6）单独确认可唤醒后再合。
- **风险**：每秒 I2C 读 VBUS — 负载小；边沿写充避免抖动重复配置。
- **风险**：Ship 测挂需 SHPHLD 通路完好；勿在无按键唤醒的夹具上盲测。
- **调试 Kconfig**：若加 `CONFIG_INK_PANEL_NPM1300_STATUS_LOG`，量产可关；非必须。

---

## 9. 与现有代码关系

- 现有 `npm1300_board_bringup` 已完成 probe + BUCK1=3.0 V + status 日志 → **扩展**充电、GPIO、poll、Ship。
- `app.c`：bring-up 后确保 poll 已启动；刷屏路径改走 `epd_power_set`（有 EPD 业务时）。
- 不改 `ble_gatt.c`；不把策略放回 cbb。
