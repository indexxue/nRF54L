# Design: ink_panel 片内 NFCT Tag 产测冒烟

**日期**：2026-11-16  
**状态**：已实现（编译通过；板上 Field/手机读待验收）  
**工程**：`projects/ink_panel/`  
**范围**：产测冒烟（Field 检测 + 固定 NDEF Text）；非业务身份标签、非配网触发  
**硬件依据**：
- `docs/superpowers/plans/schematic_wiring/07_nfc_fpc.md`（片内 NFCT + FPC2，P1.02/P1.03）
- EVT 网表 / IO 总表

---

## 1. 目标与非目标

### 目标（Must）

1. 启用 nRF54L15 **片内 NFCT**，走 NCS **NFC Type 2 Tag**（`nfc_t2t_*`）。
2. 固定 NDEF Text payload：`ink_panel NFC ok`（UTF-8 / en）。
3. 手机靠近时串口可见 `field on` / `data read` / `field off`；手机可读到上述 Text。
4. 与现有 Zigbee ED / BTN1 / LED 共存；回调不阻塞、不写 Flash。
5. Kconfig 门控 `CONFIG_INK_PANEL_NFC`（default **y**）。

### 非目标

- NVS/SN/MAC 绑定 NDEF
- Field 触发配网或其它业务
- PN5180 Reader（`eink_tag` 路线，硬件不符）
- 独立 NFC 线程 / 改 `common/event.h` / 改 Flash 分区
- 续航与 NFCT 待机电流优化（产测可接受）

---

## 2. 架构与模块边界

```text
app_init()
  └─ nfc_board_bringup()          # CONFIG_INK_PANEL_NFC=y
        ├─ nfc_t2t_setup(cb)
        ├─ 组固定 NDEF Text
        ├─ nfc_t2t_payload_set()
        └─ nfc_t2t_emulation_start()

T2T 回调 (HAL/ISR 上下文)
  └─ 只 ingress：atomic 计数 + 模块内 pending
       └─ app_run → nfc_board_poll() → printk
```

| 路径 | 职责 |
|------|------|
| `board/nfc_board.c` / `.h` | `nfc_board_bringup()`、`nfc_board_poll()`；T2T 回调 ingress；固定 NDEF |
| `src/app.c` | init 调 bringup；run 调 poll |
| `board/app.overlay` | `&nfct { status = "okay"; }` |
| `Kconfig` / `prj.conf` / `CMakeLists.txt` | 门控与 NFC/NDEF Kconfig；按开关编入源文件 |
| `README.md` | 验收说明一行 |

生命周期：沿用 `start` → `app_init` / `app_run`。  
硬约束：GATT/ISR/回调只 ingress；禁止回调内 `printk` / sleep / 刷屏 / 写 Settings；入队若将来加 msgq 须 `K_NO_WAIT`。

---

## 3. 回调与消费

| T2T 事件 | 回调动作 | `nfc_board_poll()` |
|----------|----------|-------------------|
| `NFC_T2T_EVENT_FIELD_ON` | atomic_inc + pending | 打印 `nfc: field on` |
| `NFC_T2T_EVENT_FIELD_OFF` | 同上 | `nfc: field off` |
| `NFC_T2T_EVENT_DATA_READ` | 同上 | `nfc: data read` |
| 其它 | 忽略 | — |

- **不**使用公共 `EVT_ID_*`（避免改 `common/event.h`）。
- **不**新建线程；`app_run` 已有 20 ms 圈，足够冒烟。
- 事件稀疏；满/丢策略：atomic 累加，poll 时读差量打印（可每类打一行并清零）。

---

## 4. Kconfig / DT / 合入

| 项 | 决定 |
|----|------|
| `CONFIG_INK_PANEL_NFC` | bool，default **y**；关则不编 `nfc_board`、app 跳过 |
| `prj.conf` | `CONFIG_INK_PANEL_NFC=y`；`CONFIG_NFC_T2T_NRFXLIB=y`；NDEF msg/record/text 所需项 |
| `&nfct` | overlay `okay`（关功能时不 start 即可） |
| 引脚 | P1.02=`NFC1`、P1.03=`NFC2`（勿配成 GPIO） |
| Flash | 本轮不写 Settings |

参考 NCS sample：`nrf/samples/nfc/record_text`。

---

## 5. API（板级）

```c
/** setup T2T + 固定 NDEF + emulation_start；成功 STATUS_OK */
status_t nfc_board_bringup(void);

/** 主循环调用：消费 pending，串口打 field/data 日志 */
void nfc_board_poll(void);
```

命名：`module_verb_object()`。非必要不建公共头以外文件。

---

## 6. 验收

| # | 标准 |
|---|------|
| 1 | `ink_panel` 编译通过（Zigbee 仍链入） |
| 2 | 串口：`nfc: bringup ok` |
| 3 | 手机靠近：`nfc: field on` →（读标签）`nfc: data read` → 移开 `nfc: field off` |
| 4 | 手机显示 Text：`ink_panel NFC ok` |
| 5 | BTN1 短按配网仍可用 |

失败：`nfc: bringup fail err=...`；无天线/无场则只有 bringup 行。

---

## 7. 修订

| 版本 | 日期 | 说明 |
|------|------|------|
| 0.1 | 2026-11-16 | 初稿：方案 1 T2T 产测冒烟；对话 §1–§3 批准 |
