# Design: ink_panel（nRF54L15 EVT 墨水屏终端 · 第一轮 bring-up）

**日期**：2026-11-12  
**状态**：对话设计已批准；待规格审阅后写实现计划  
**产品**：Zigbee 终端墨水屏显示器（Sleepy ED 方向；本轮非 Sleepy 定稿）  
**工程名**：`ink_panel` → `projects/ink_panel/`  
**硬件依据**：
- `docs/superpowers/plans/nRF54L15 墨水屏 Zigbee SED 硬件技术文档（评审稿）.md`
- 网表 `docs/superpowers/plans/Netlist_Schematic1_2026-11-05.tel`
- IO 总表 `docs/superpowers/plans/schematic_wiring/01_mcu_io_map.md`

---

## 1. 目标与非目标

### 目标（第一轮 Must）

1. 新建独立工程 `projects/ink_panel`，**不修改** `zigbee_ed` 运行时行为。
2. 脚手架：**整树复制** `projects/zigbee_ed`（方案 1），含 MCUBoot sysbuild / `pm_static.yml` / `zboss_nvram`。
3. 板级改为 EVT 网表：UART + 单 LED + BTN1 + HFXO + Zigbee ED。
4. Zigbee ED 栈可起机；BTN1 短按开配网窗 / Network Steering；**可被协调器发现**。
5. 串口 115200 可观察起机 / heartbeat / `zigbee: steering` 等日志。
6. 保留 OnOff Server 作联调探针（**非产品语义**，README 标明）。

### 非目标（第一轮）

- 墨水屏 SSD1680、CAP1203、nPM1300、SPI NOR
- Touchlink、涂鸦 PID/DP 产品身份定稿
- 删掉 OnOff / 厂测长按等联调逻辑（瘦身留后轮）
- 新建完整 Zephyr board 包（仍借 `nrf54l15dk/nrf54l15/cpuapp` + overlay）
- Sleepy ED poll 周期与续航策略定稿

---

## 2. 工程与模块边界

| 路径 | 职责 |
|------|------|
| `projects/ink_panel/src/app.c` | 灯效 / 按键 / 串口日志绑定 |
| `projects/ink_panel/lib/zigbee/` | 自 `zigbee_ed` 拷贝；Model → `ink_panel` |
| `projects/ink_panel/board/` | EVT overlay + `board_config` |
| `start/` + `common/` | 仓库共享；本轮不改 API |
| `nrf54_ble` / EPD / cbb 面板 | 不纳入 |

生命周期：`start` → `app_init` / `app_run`。Zigbee / 按键回调仅 **ingress**（msgq / work / atomic）；禁止回调内 sleep、刷屏、写 Settings。

---

## 3. 板级 IO（EVT 网表）

| 功能 | 网名 | GPIO | 备注 |
|------|------|------|------|
| UART TX | `UART_TX` | P2.01 | `uart20` → USB-UART `U16` → Type-C |
| UART RX | `UART_RX` | P2.00 | pull-up；验收须插 USB 给 `U16` 供电 |
| 状态灯 | `LED_STATUS_N` | P1.13 | ACTIVE_LOW；仅 `led0` |
| 用户键 | `BTN_USER_N` | P0.00 | BTN1：配网窗 / 厂测长按沿用 |
| HFXO | `X1` | — | `&hfxo` okay；负载电容先沿用 zigbee_ed 内置上限策略 |
| Zigbee timer | — | timer20 | `ncs,zigbee-timer` |

### 刻意不做的接线

- **禁止**把 `P0.01`（`TOUCH_ALERT_N`）映射为 BTN2。
- `led1`/`led2`/`led3` disable；`board_config` 只注册 1 颗 LED。
- NFC / EPD / I²C / NOR 本轮不启用。

### 单灯 / 无 BTN2 行为

- `led_scene` 双通道场景退化成单灯驱动，可接受。
- BTN2 相关 leave / 擦 `zboss_nvram` 手势：**无硬件入口**；README 标明；清 NV 用调试器或后轮补命令。

---

## 4. Zigbee 行为（本轮）

| 项 | 取值 |
|----|------|
| 角色 | End Device（联调 `rx_on_when_idle` 策略沿用 `zigbee_ed`） |
| 发现 | BTN1 短按 → 配网窗 + Network Steering |
| 信道掩码 | 沿用 `CONFIG_ZIGBEE_CHANNEL_MASK=0x02108800`（11/15/20/25） |
| Basic Model | `ink_panel`（Manufacturer 暂沿用联调占位，如 `TY`） |
| OnOff EP1 | 保留作探针；非产品灯控语义 |

成功判据：**协调器侧可发现该 ED**；不要求本轮完成产品入网/业务定稿。

---

## 5. 实现步骤

1. 复制 `projects/zigbee_ed` → `projects/ink_panel`。
2. 改名：`CMakeLists.txt` `project(ink_panel)`、board 名、Kconfig Model、README、串口字符串。
3. 重写 `board/app.overlay`（上表引脚）。
4. 改 `board/board_config.c`：单 LED、单按键、mask 收窄。
5. 改 `src/app.c`：去掉对 BTN2 / 第二颗 LED 的硬依赖或无入口路径。
6. 根 `README.md` 工程列表增加 `ink_panel` 一行。
7. `.\nrf.cmd -Project ink_panel build`；有板再 `flash`。

---

## 6. 验收

1. `build` 通过。
2. `flash` 后串口见 `ink_panel` 起机 / heartbeat。
3. LED 随 Zigbee 状态有 `led_scene` 变化。
4. BTN1 短按后串口 `zigbee: steering`；协调器可发现。
5. OnOff 控灯通为加分项，非 Must。

---

## 7. 风险

| 风险 | 缓解 |
|------|------|
| HFXO 负载与 EVT 晶振不匹配 | 先沿用现策略；不起振再按规格书调 femtofarad |
| 无 USB 时无串口 | 验收必须插 Type-C（`U16` 靠 VBUS） |
| 与 `zigbee_ed` 协议分叉 | 本轮接受；公共化后轮评估 |
| 无 BTN2 无法 leave/擦 NV | README 写明；J-Link 擦或后轮串口命令 |

---

## 8. 合入

- 新工程目录合入即可；不改动 `zigbee_ed` 默认行为。
- 调试用 Zigbee traces 等 Kconfig 沿用源工程；量产关闭策略后轮再定。
