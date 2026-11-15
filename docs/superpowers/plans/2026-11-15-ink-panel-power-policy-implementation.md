# ink_panel 产品电源策略 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `ink_panel` 板级落地 USB-C/电池运行：BUCK1=3.0V、VBUS 轮询充放电、GPIO 门控触摸/EPD、显式 Ship。

**Architecture:** 策略全部在 `npm1300_board.*`；`cbb/npm1300` 不改。1 s 轮询挂在现有 `app_run` 循环（不新建线程）。轨门控走 DT `zephyr,user` GPIO。

**Tech Stack:** Zephyr/NCS、cbb `npm1300`、GPIO DT、UART 日志。

**Spec:** `docs/superpowers/specs/2026-11-15-ink-panel-power-policy-design.md`

## Global Constraints

- GATT/ISR：禁止 sleep / 刷屏 / Settings；本模块仅线程/`app_run` 调用
- 入队若有：`K_NO_WAIT`；本阶段无 msgq
- Settings：零写入
- 引脚：`EPD_PWR_EN` P0.02、`TOUCH_LDO_EN` P0.03 走 overlay
- 命名：`npm1300_board_*`；不新建 `.c` 文件
- ICHG 改前先 `charger_enable(false)`
- VBUS 判定：`VBUSINSTATUS` bit0（`VBUSDETECTED`）

## Review Focus

- 无 VBUS 时误开充 → poll 必须看 bit0 与 inhibit
- 每秒重写 ICHG → 仅边沿/期望态变化时配置
- GPIO 未进 DT → 编译或静默无门控
- Ship 后仍打 I2C → `enter_ship` 末尾不再访问总线
- J-Link 调试路径 ≠ 量产验收

---

### Task 1: Overlay GPIO + 轨门控 API

**Files:**
- Modify: `projects/ink_panel/board/app.overlay`
- Modify: `projects/ink_panel/board/npm1300_board.h`
- Modify: `projects/ink_panel/board/npm1300_board.c`

**Interfaces:**
- Produces: `npm1300_board_epd_power_set(bool on)`、`npm1300_board_touch_power_set(bool on)`

- [x] **Step 1:** `zephyr,user` 增加 `epd-pwr-gpios`（gpio0 2 ACTIVE_HIGH）、`touch-ldo-gpios`（gpio0 3 ACTIVE_HIGH）
- [x] **Step 2:** 实现 configure + set；bring-up 内 touch=on、epd=off
- [x] **Step 3:** 编译通过

---

### Task 2: 充电配置 + inhibit + poll

**Files:**
- Modify: `npm1300_board.h` / `.c`
- Modify: `projects/ink_panel/src/app.c`

**Interfaces:**
- Produces: `npm1300_board_charge_inhibit_set(bool)`、`npm1300_board_poll_charge(void)`
- Constants: ICHG=300、VTERM=4200、poll 周期由 `app_run` 每 1 s 调一次

- [x] **Step 1:** RAM `charge_inhibit`、`charge_want`；`apply_charge(want)` 边沿写
- [x] **Step 2:** bring-up 末尾按 VBUS 应用一次；`poll_charge` 读 bit0
- [x] **Step 3:** `app_run` 每 50 tick（1 s）调 `poll_charge`
- [ ] **Step 4:** 板上插拔 Type-C 看 `chg`/日志（人工）

---

### Task 3: Ship API

**Files:**
- Modify: `npm1300_board.h` / `.c`

**Interfaces:**
- Produces: `int npm1300_board_enter_ship(void)` — inhibit → 关充 → epd off → `ship_enter`

- [x] **Step 1:** 实现 API；成功后不再 I2C
- [x] **Step 2:** 编译通过；Ship 实测单独做（勿盲测无按键夹具）

---

### Task 4: Spec 状态 + 短验收备注

- [ ] 规格文档状态改为「实现中/已实现」
- [ ] 串口期望：起机 `npm1300 ok`；有 VBUS 时 poll 后可开充
