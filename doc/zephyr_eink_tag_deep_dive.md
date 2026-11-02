# Zephyr 深挖指南（面向 `eink_tag`）

本文给本仓库 **`projects/eink_tag`** 用：在你已有「应用级 Zephyr」基础上，按产品痛点深挖，而不是通读手册。  
对照工程：[`projects/eink_tag/README.md`](../projects/eink_tag/README.md)、[`ble/README.md`](../ble/README.md)、[`start/`](../start/)。

SDK：本机 NCS **3.2.x**（Zephyr 4.2 系）。官方文档以 NCS / Zephyr 对应版本为准。

---

## 0. 你现在站在哪一层

```
L0  工具链 / west / nrf.cmd / prj.conf / overlay     ← 已会
L1  驱动 + DT（GPIO/UART/SPI）+ Settings/NVS         ← 已会
L2  内核原语（thread / msgq / mutex / sleep）        ← 已在用
L3  BLE Host 回调边界 + 优先级 / 栈 / 延迟分析        ← 建议下一刀
L4  电源、时钟、Radio 共存、OTA(MCUBoot)            ← 量产前
L5  自写 driver / 子系统 / FLPR 卸载刷屏             ← 按痛点选做
```

目标不是「成为 Zephyr 内核开发者」，而是：**刷屏不拖死 BLE、连接稳、可测、可 OTA、可上板省电**。

---

## 1. 用 `eink_tag` 当实验室：代码地图

把 Zephyr 概念钉到具体文件上读，比抽象教程快。

| Zephyr 概念 | 本工程落点 | 建议你问自己的问题 |
|-------------|------------|-------------------|
| `main` 已是线程 | [`start/src/main.c`](../start/src/main.c) → `app_run` | 为什么还能再 `k_thread_create`？ |
| 周期 poll | [`start/src/start.c`](../start/src/start.c) `start_poll`；[`app.c`](../projects/eink_tag/app.c) 20 ms 环 | 哪些该改成 timer / work，而不是轮询？ |
| 工作线程 | `app.c` 里 `s_epd_thread` + `s_epd_jobs` | 优先级 8 相对 BLE / main 谁高？刷屏时 GATT 还响应吗？ |
| 消息队列 | [`ble/src/ble_cmd.c`](../ble/src/ble_cmd.c) raw/parsed msgq；EPD job msgq | 队列满了丢谁？ISR/回调里能否 `K_FOREVER`？ |
| 互斥 | [`common/src/log.c`](../common/src/log.c) | 持锁期间能否 `k_sleep` / 刷屏？ |
| Devicetree | [`board/app.overlay`](../projects/eink_tag/board/app.overlay) | `status`、`pinctrl`、与 DK 默认节点冲突怎么关？ |
| Settings | [`common/src/nvs.c`](../common/src/nvs.c) + `prj.conf` Settings | key 命名、加载失败默认值、写频率？ |
| BLE Host | [`ble/src/`](../ble/src/) `ble_stack` / `ble_gap` / `ble_gatt` / `ble_cmd_thread` | 哪些 API 禁止在 RX 回调里直接调？ |
| 时钟 | `CONFIG_START_REQUEST_HFXO` + RC LFCLK | BLE 为何必须 HFXO？关掉会怎样？ |
| 分区 | 工程 `pm_static.yml` / [`flash_layout.md`](flash_layout.md) | `settings_storage` 与以后 MCUBoot 槽怎么衔接？ |

**推荐阅读顺序（只读本仓库）：**

1. `app.c`：EPD 线程与 BLE CMD 的交界  
2. `ble_gatt.c` → `ble_cmd_ingress` → `ble_cmd_thread` → `app_handle_ble_cmds`  
3. `source/epd.c`：为何 bitbang、BUSY、为何不能堵在 `main`  
4. `board/app.overlay` + `board_config.c`：DT 如何变成 `board_config_t`  
5. `common/nvs.c`：Settings handler 与面板尺寸持久化  

---

## 2. 深挖路线（按阶段，绑定 `eink_tag` 验收）

每阶段只做一件事，并规定 **可观察验收**（串口 / nRF Connect / 逻辑分析仪任选）。

### 阶段 A — 线程与延迟（1–2 周）★ 优先

**学什么：** 线程优先级、抢占、`k_msgq` 满、回调上下文、栈溢出。

**在工程里做：**

| 实验 | 做法 | 通过标准 |
|------|------|----------|
| A1 量刷屏卡顿 | 刷屏中连发 PING，看 ACK 延迟与串口 `status` | 能说出「慢在哪条线程」 |
| A2 改 EPD 优先级 | 试 `APP_EPD_THREAD_PRIO` 相对 main / ble_cmd | 记录：谁饿死、谁抖动 |
| A3 队列压测 | CHUNK 狂发，观察 msgq 满与 ACK 错误码 | 有明确背压策略（丢/拒/流控） |
| A4 栈 | 临时加大/减小 `APP_EPD_STACK_SIZE`，开 `CONFIG_STACK_SENTINEL` 或 thread analyzer | 知道刷屏栈下限 |

**建议打开的 Kconfig（调试用，量产再关）：**

```
CONFIG_THREAD_NAME=y
CONFIG_THREAD_ANALYZER=y          # 或按 NCS 版本文档等价项
CONFIG_THREAD_ANALYZER_AUTO=y
CONFIG_STACK_SENTINEL=y
```

**官方关键词：** Zephyr *Kernel Synchronization*、*Threads*、*Workqueue Queue*。

---

### 阶段 B — Devicetree 与驱动边界（1 周）

**学什么：** overlay 覆盖顺序、`pinctrl`、节点 `disabled`、驱动绑定 vs 自管 GPIO。

**在工程里做：**

| 实验 | 做法 | 通过标准 |
|------|------|----------|
| B1 读编译产物 | 打开 `build/eink_tag/zephyr/zephyr.dts`（或 sysbuild 下对应路径） | 能指认 uart20 / spi00 / LED 最终脚 |
| B2 故意冲突 | 临时打开与 UART 冲突的 PWM/按键节点 | 理解「为何 demo/eink overlay 要 disable」 |
| B3 SPI vs bitbang | 对照 `epd.c` 里 release SPIM、改 bitbang | 能画出「硬件 SPI 何时可用、为何现在 bitbang」 |
| B4 自定义 board | 读 [`boards/README.md`](../boards/README.md)，列自有板 DTS 清单 | 不再长期借 `nrf54l15dk` 名 |

**官方关键词：** *Devicetree HOWTOs*、*Pin Control*、Nordic *pinctrl*。

---

### 阶段 C — BLE 主机模型（1–2 周）

**学什么：** `bt_enable` 后谁在跑；连接回调上下文；Notify 与 MTU；与应用线程的边界。

**在工程里做：**

| 实验 | 做法 | 通过标准 |
|------|------|----------|
| C1 回调纪律 | 在 GATT write 回调里故意 `k_sleep`（仅实验） | 亲眼见异常后改回「只 ingress」 |
| C2 MTU/长度 | 调 `prj.conf` ATT/ACL，传大 CHUNK | 与 [`ble/docs/display_plane_upload.md`](../ble/docs/display_plane_upload.md) 对照 |
| C3 连接参数 | 试改连接间隔（需查 NCS BLE API） | 刷屏中吞吐 vs 功耗有数据 |
| C4 断线重广 | 拔手机、看 `ble_gap` 日志与重播 | 状态机与 `HEARTBEAT` 标志一致 |

**官方关键词：** NCS *Bluetooth API*、Zephyr *Bluetooth Stack Architecture*。  
**本仓库锚点：** [`ble/`](../ble/) 已把「栈」和「CMD 业务」分开——深挖时保持这条边界，不要把业务塞回 GATT 回调。

---

### 阶段 D — Settings / Flash / 以后 OTA（量产向）

**学什么：** Settings 后端、写磨损、分区、MCUBoot 双槽（布局已在 [`flash_layout.md`](flash_layout.md) 锁定）。

**在工程里做：**

| 实验 | 做法 | 通过标准 |
|------|------|----------|
| D1 面板 NVS | 双击/长按切 2.9↔4.2，掉电再读 | 与 `nvs_epd_panel_*` 一致 |
| D2 写频率 | 统计哪些路径会 `settings_save` | 无「每秒写 flash」 |
| D3 读 PM 输出 | `build/.../partitions.yml` | 能讲清 app vs settings_storage |
| D4 MCUBoot（后） | 按 plan 启用双槽，不先改业务 | 单独里程碑，勿与刷屏重构绑死 |

---

### 阶段 E — 电源与时钟（上板后）

**学什么：** HFXO onoff、RADIO 活动、idle、外设 clock。

**在工程里做：**

| 实验 | 做法 | 通过标准 |
|------|------|----------|
| E1 HFXO | 对比关/开 `CONFIG_START_REQUEST_HFXO` | BLE 失败模式可复述 |
| E2 电流（有条件） | 广播 / 连接 / 刷屏三段电流 | 有一张自己的表 |
| E3 刷屏能耗 | 全刷次数策略（宿主刷新 vs demo） | 产品策略有据 |

---

### 阶段 F — 可选深水区（有痛点再进）

| 主题 | 何时做 | 与 `eink_tag` 的关系 |
|------|--------|----------------------|
| System workqueue / `k_work` | 想减少「专用线程」时 | 可评估 LED/轻量事件；PN5180 读卡勿放 GATT 回调 |
| 自写 Zephyr driver | 外设要进 upstream 风格或被多 app 复用 | EPD 现走 `cbb` + board hook，一般不必强行 driver 化 |
| Logging 子系统 | 需要结构化/多后端日志时 | 现用自有 `log.h`，换 Zephyr LOG 要解决宏冲突 |
| FLPR 卸载刷屏 | 刷屏与 BLE 抢时间且单核优化不够 | 见 [`projects/demo`](../projects/demo/README.md) 双核脚手架；边界切在「平面已在 RAM → SPI 吐屏」 |
| 自定义 `boards/` | 原理图稳定 | 替代长期借用 DK board 名 |

---

## 3. 建议的「每周练习」模板（可直接复制）

```text
本周主题：______________（例如 A2 EPD 优先级）
改动文件：
现象（改前）：
现象（改后）：
结论（一句话）：
是否合入主线：是 / 否（实验分支）
```

原则：

- **实验用分支**，避免把调试 Kconfig 长期留在量产 `prj.conf`  
- 一次只改一个变量（优先级或栈或队列深度）  
- 验收必须能给别人复述，而不是「感觉好了」

---

## 4. 官方资料怎么啃（省时间）

按需打开，不要按目录通读：

| 需求 | 去哪 |
|------|------|
| 线程 / msgq / work | Zephyr Docs → Kernel |
| Overlay / pinctrl | Zephyr Docs → Devicetree + Nordic HW Guide |
| BLE API | nRF Connect SDK Documentation → Bluetooth |
| 分区 / PM | NCS → Partition Manager；对本仓库再对 [`flash_layout.md`](flash_layout.md) |
| 样例对照 | NCS `zephyr/samples`、`nrf/samples`；双核见 `sysbuild/hello_world`、`drivers/mbox` |

本机 SDK 路径（默认）：`D:\ncs\v3.2.0\zephyr\`、`D:\ncs\v3.2.0\nrf\`。

---

## 5. 和 FreeRTOS / STM32 习惯的对照

| 你熟悉的 | Zephyr / 本仓库对应 |
|----------|---------------------|
| `osThreadNew` | `k_thread_create` / `K_THREAD_DEFINE` |
| 队列 | `k_msgq`（定长）；大数据用指针 + 自管缓冲 |
| 二值/计数信号量 | `k_sem`；互斥用 `k_mutex`（注意优先级继承语义） |
| CubeMX 引脚 | Devicetree overlay + `pinctrl` |
| HAL 句柄全局 | `DEVICE_DT_GET` / `gpio_dt_spec` |
| 自己写 main 起调度器 | Zephyr 已跑内核；`main` 只是一个线程 |
| 应用框架 | 本仓库 `start_boot` → `app_init` → `app_run`（刻意保持跨 MCU 风格） |

深挖时 **保留** `start`/`app` 边界；把精力放在「Zephyr 原语用对」，而不是把框架改成纯 Zephyr sample 风格。

---

## 6. 针对 `eink_tag` 的「深挖完成」定义（务实）

可以认为阶段 A–C 达标，当你能书面回答：

1. 刷屏时 BLE ACK 变慢，延迟预算卡在哪条线程、哪次锁、哪次 flash？  
2. GATT write 路径上，从回调到 EPD job，每一跳能否阻塞？满了怎么办？  
3. `zephyr.dts` 里最终脚位与原理图一致吗？  
4. 面板尺寸存在 Settings 的哪把 key？掉电谁加载？  
5. 若上 MCUBoot，当前 `pm_static` 哪一段必须改？（可先答到「对照 flash_layout」，实现可后置。）

阶段 D–E 留给量产门禁；阶段 F 仅在测量证明有必要再开。

---

## 7. 明确不要做的事

- 为了「更 Zephyr」而重写整套 `log` / `start`（无产品收益）  
- 未测延迟就上 FLPR 双核（先做阶段 A）  
- 在 GATT 回调里直接 `epd_board_refresh`  
- 无原理图就深挖自定义 SoC driver  
- 把官方 sample 整目录拷进 `projects/eink_tag` 搅乱模块边界  

---

## 8. 相关文档

| 文档 | 用途 |
|------|------|
| [`getting_started.md`](getting_started.md) | 环境与开工程 |
| [`plan.md`](plan.md) | 仓库阶段计划 |
| [`flash_layout.md`](flash_layout.md) | RRAM / 以后 OTA |
| [`decisions.md`](decisions.md) | 架构决策 |
| [`projects/eink_tag/README.md`](../projects/eink_tag/README.md) | 产品冒烟与协议 |
| [`projects/demo/README.md`](../projects/demo/README.md) | FLPR 双核脚手架（可选） |

---

*文档定位：学习与工程深挖路线，不替代 NCS 官方手册。随 `eink_tag` 行为变更时，优先改本文「代码地图」与阶段验收表。*
