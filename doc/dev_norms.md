# 开发规范（复杂需求门禁）

面向本仓库 nRF54L15 / Zephyr（NCS 3.2.x）产品开发。  
**简单改动**（改文案、修笔误、单点 bug）可跳过门禁；**稍复杂需求**必须先过下方清单再写代码。

对照：[`zephyr_eink_tag_deep_dive.md`](zephyr_eink_tag_deep_dive.md)、[`decisions.md`](decisions.md)、[`flash_layout.md`](flash_layout.md)。

---

## 1. 何时算「稍复杂」（必须先提出规范）

满足任一条即启动门禁：

- 新线程 / 改优先级 / 新 `k_msgq` / workqueue
- BLE GATT、连接参数、CMD 协议、Notify/MTU
- 刷屏、SPI、长耗时外设、与 BLE 并行
- Settings/NVS 新 key、写 Flash、分区 / MCUBoot / OTA
- 新 overlay 引脚、自定义 board、与 DK 节点冲突
- 跨 `start` / `app` / `ble` / `common` / `cbb` 的边界改动
- 预估改动 ≥ 3 个模块，或行为无法用一次串口冒烟说清

门禁输出格式（实现前贴给评审/自己）：

```text
【需求】一句话
【上下文】跑在：回调 / main / 工作线程 / ISR？
【能否阻塞】最长多久？失败对端看到什么？
【队列/锁】深度、满了策略（拒/丢/流控）
【Flash】是否写 Settings？频率？
【共存】刷屏/忙时 PING 或关键路径是否仍可用？
【验收】可观察标准（串口 / nRF Connect / 电流）
【合入】实验分支？调试 Kconfig 是否量产关闭？
```

---

## 2. 架构边界（不可破）

| 层 | 职责 | 禁止 |
|----|------|------|
| GATT / ISR / 定时器回调 | 只 ingress：`msgq` / `k_work` / atomic | `k_sleep`、刷屏、Settings 写、大块业务 |
| `ble_cmd` 线程 | 解析 raw → parsed | 产品决策、EPD |
| `app_*`（main 环） | 产品逻辑、ACK、入 EPD 队 | 秒级刷屏堵死 poll |
| EPD / 慢外设线程 | 长耗时 I/O | 在回调里直接调用 |
| `start/` | 平台起机、`start_poll`、`K_EVENT_DEFINE(app_events)` | 塞产品协议 |
| `common/nvs` | Settings 键值 API（业务壳，非自研 NVS） | 业务里散落 `settings_save` |
| 唤醒同步 | Zephyr `k_event`（`app_event.h`） | 自研 event/sem 包装 |
| CRC | `zephyr/sys/crc.h` `crc32_ieee` | 自研 crc32 表 |

生命周期固定：`start_boot` → `app_init` → `app_run`。  
Phase 1A 已迁：`k_event` / `crc32_ieee`；已删自研 `event` / `crc32` / `input`。  
**暂不**为「更 Zephyr」重写 `log` / `button` / `flexible_button` / `led_scene`；不整目录拷官方 sample。  
后续 Phase 可再议 log / gpio-keys，须单独过门禁。

---

## 3. 并发与背压

1. Zephyr 优先级：**数字越小越高**。链路/控制 > 慢外设（例：`ble_cmd` 5，`epd` 8）。
2. 生产者入队一律 **`K_NO_WAIT` + 明确错误码**；仅消费者可用 `K_FOREVER`。
3. 队列满必须有策略：拒（ATT/ACK 错码）、丢、或应用层流控——禁止静默堵死协议栈。
4. 大缓冲（显示平面）用静态 RAM + offset 协议；**不要**把整帧塞进 msgq。
5. 共享「忙/就绪」优先 `atomic`；短临界区用 `k_mutex`，**锁内禁止** sleep / 刷屏 / 写 Flash。
6. 一次实验只改一个变量：优先级 **或** 栈 **或** 队列深度。

---

## 4. BLE / GATT

- Write 回调 → 只 `ble_cmd_ingress`（本仓库已拆分，保持边界）。
- ACK / HEARTBEAT 走 TX Notify；未开 Notify 要有明确跳过/错误，不假成功。
- 改 ATT/MTU/连接间隔前：对照 `ble/docs/`，并做刷屏中吞吐与空闲对照。
- 手机侧用自定义 128-bit 服务；不用 SMP/DFU 菜单测业务 CMD。

---

## 5. Flash / Settings / 分区

- Settings 路径：`ty_app/<key>`（如面板 `ty_app/epanel`）；经 `nvs_*` API。
- **事件驱动写**（改配置时），禁止周期/每秒写。
- 运行中 APP 只写 staging + `settings_storage`；不写 Boot / 运行槽（量产规则）。
- 预 MCUBoot：`app` + `settings_storage@0x15C000`；上 OTA 换成 [`flash_layout.md`](flash_layout.md) 双槽，**与刷屏重构解耦**。

---

## 6. Devicetree / 板级

- 引脚只经 overlay / `board_config` / `gpio_dt_spec`；业务禁止硬编码 GPIO 号与 Flash 地址。
- 与 DK 冲突节点：`status = "disabled"`。
- 改脚验收：编译产物 `zephyr.dts` 与原理图/README 一致。

---

## 7. 编码规范

| 项 | 约定 |
|----|------|
| 命名 | `module_verb_object()`：`nvs_epd_panel_get`、`ble_cmd_ingress` |
| 文件 | 小写+下划线，`.c`/`.h` 同名；非必要不建新文件 |
| 守卫 | `#ifndef MODULE_H` |
| 返回值 | 驱动/存储 `status_t`；简单成败 `bool`；底层偶用 `int` |
| include | 自身 `.h` → 项目模块 → Zephyr/NCS → 标准库 |
| 风格 | 跟周围文件；4 空格 |
| 禁止 | 裸名 `process()`；改 SDK `Drivers/`；ISR 里 Flash/NVS |

---

## 8. 原语选用（什么情况用什么）

| 场景 | 用 | 不用 |
|------|-----|------|
| BLE/IRQ 收包 | `k_msgq` + `K_NO_WAIT` 或 `k_work` | 回调里业务 |
| 秒级刷屏 / bitbang | 专用较低优先级线程 | main / GATT |
| 忙标志 | `atomic` | 无保护 bool |
| 短互斥（如 log 组包） | `k_mutex`（锁内尽量短） | 锁内长 I/O |
| 周期轻量事 | main poll 或 `k_timer` + work | 每个事件一个线程 |
| 掉电配置 | `nvs_*` / Settings，低频 | 裸擦 Flash；高频写 |
| 引脚/外设 | DT + `DEVICE_DT_GET` | 魔数寄存器 |
| 延迟分析未完成 | 先测线程/队列 | 先上 FLPR 双核 |

---

## 9. 开发检查清单（提交前）

- [ ] 代码跑在正确上下文；回调路径无阻塞
- [ ] 队列/锁失败路径有日志 + 对端可见错误
- [ ] 无「每秒写 Flash」
- [ ] 忙时关键路径（如 PING）仍可在合理时间内完成，或已文档化限制
- [ ] 调试用 `THREAD_ANALYZER` / `STACK_SENTINEL` 等不留在量产 `prj.conf`
- [ ] 实验在分支；结论可复述（数字/日志），非「感觉好了」

---

## 10. 明确不要做

- GATT 回调里直接 `epd_board_refresh` / `k_sleep`
- 未测延迟就上 FLPR
- 无原理图深挖自定义 SoC driver
- 把业务塞回 `ble_gatt.c`
- 为整齐而新建无必要的头文件/脚本/文档

---

*随 `eink_tag` / 公共模块行为变更时，优先更新本文件 §2–§5 与门禁模板。*
