# Design: zigbee_demo（nRF54L15 Zigbee End Device 联调）

**日期**：2026-10-13  
**状态**：已批准（对话设计）  
**需求来源**：`doc/req_nrf54_zigbee_end_device.md`  
**板级参考**：`projects/eink_tag`（rev_b 脚，仅 UART/LED/按键）

---

## 1. 目标与非目标

### 目标（Must）

1. 新建独立工程 `projects/zigbee_demo`，不破坏现有 BLE 量产路径（`eink_tag` / `demo` / `factory`）。
2. 角色：Zigbee **End Device**（非 Sleepy）；禁止 Coordinator。
3. BTN1 短按发起 Network Steering；BTN2 长按 leave / 恢复出厂网络信息。
4. 信道默认含 **Channel 11**（与 ZigBridge-EM 协调器一致）。
5. 串口 + LED 指示：搜网中 / 已加入 / 失败 / 已退网；成功时打印本机 **IEEE** 与 **NWK**。
6. 可编译 + README（构建板型、烧录、按键、信道、联调步骤）。

### 非目标（本期）

- 墨水屏 EPD、PN5180/NFC、BLE、BLE+Zigbee 共存  
- OnOff / Identify 等 ZCL 业务集群  
- Sleepy ED、install code、量产证书、MQTT / 设备表  

---

## 2. 工程与模块边界

脚手架：`scripts/new_project.ps1 -Name zigbee_demo`（模板 `projects/demo`）。

| 纳入 | 不纳入 |
|------|--------|
| `nrf54_common` + `nrf54_start` | `nrf54_ble`、`CONFIG_BT*` |
| eink_tag rev_b：UART + LED×2 + BTN×2 overlay / `board_config` | EPD、SPI、PN5180、`cbb/epd` |
| Zigbee R23 add-on（ZBOSS），ED + BDB steering/leave | Coordinator、OnOff/Identify |

生命周期：`start` → `app_init` / `app_run`；Zigbee / 按键回调仅 **ingress**（msgq / work / atomic），禁止在回调中 sleep、写 Settings、长阻塞。

---

## 3. 行为设计

### 3.1 按键

| 按键 | 引脚（rev_b） | 动作 |
|------|----------------|------|
| BTN1 | P0.00 | 短按 → Network Steering |
| BTN2 | P0.01 | 长按 → leave / factory reset 网络信息 |

### 3.2 应用状态

`IDLE` → `STEERING` → `JOINED` | `FAILED`；leave 后回 `IDLE`。

栈回调入队 → 工作线程 / `app` 路径消费 → 更新 LED + 串口。入队使用 `K_NO_WAIT`；仅消费者可 `K_FOREVER`。

### 3.3 可观测

| 通道 | 内容 |
|------|------|
| 串口 | `steering` / `joined` / `failed` / `left`；joined 时 IEEE + NWK |
| LED | 搜网闪；已加入常亮；失败快闪；leave 后熄或回空闲态（实现用现有 `led_scene`） |

### 3.4 联调顺序（写入 README）

1. 网关：`znp start` → `znp join on 180`  
2. 本机：烧录后 BTN1 短按  
3. 网关见 `[ZNP] join nwk=… ieee=…`，与本机串口 IEEE 对照  
4. BTN2 长按 leave，再入网至少一次  

---

## 4. 目录与文件

```text
projects/zigbee_demo/
  CMakeLists.txt          # common + start；无 ble / epd
  prj.conf                # UART/GPIO/HFXO + Zigbee ED
  board/app.overlay       # UART + LED + BTN×2（自 eink_tag 裁）
  board/board_config.c
  src/app.c               # 按键 → steering/leave；状态 → LED/串口
  README.md
  sysbuild.* / pm_static  # 先跟 demo；Zigbee 分区冲突再单独立项
```

---

## 5. SDK 与版本

- 使用支持 nRF54L15 的 **Zigbee R23 add-on**；实现时核对与仓库 NCS **3.2.x** 兼容性。  
- 若 add-on 钉在更旧 NCS：本工程 README 单独说明工具链，**不**改全仓 BLE 工程 SDK。  
- 栈侧参考官方 Light Switch，裁掉 OnOff/Identify，只留 ED + BDB steering/leave。

---

## 6. 验收（A1–A5）

| ID | 标准 |
|----|------|
| A1 | ED 角色运行 Zigbee 固件 |
| A2 | 网关 permit join 窗口内入网 |
| A3 | 网关 join 日志 IEEE 与本机一致 |
| A4 | leave 后可再入网 ≥1 次 |
| A5 | 不改动 eink_tag / ZigBridge 主线 |

---

## 7. 决策记录

| 项 | 选择 |
|----|------|
| 交付边界 | 联调完整（入网 + LED/按键），非仅板级冒烟 |
| ZCL | 最小：无 OnOff/Identify |
| 按键 | BTN1 入网；BTN2 长按 leave |
| 墨水屏 | 本期不做 |
| 工程形态 | 方案 1：demo 脚手架 + eink 脚 + Zigbee add-on |
| 工程名 | `zigbee_demo` |
