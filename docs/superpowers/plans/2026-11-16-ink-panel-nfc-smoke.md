# ink_panel NFC T2T 产测冒烟 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 `ink_panel` 启用片内 NFCT Type 2 Tag，固定 NDEF Text，串口可见 Field/读标签事件。

**Architecture:** 板级 `nfc_board_*` 封装 NCS `nfc_t2t_*` + NDEF Text；T2T 回调只 atomic ingress；`app_run` 调 `nfc_board_poll()` 打日志。Kconfig `INK_PANEL_NFC` 门控。

**Tech Stack:** Zephyr/NCS、`CONFIG_NFC_T2T_NRFXLIB`、NDEF Text record、UART `printk`。

**Spec:** `docs/superpowers/specs/2026-11-16-ink-panel-nfc-smoke-design.md`

## Global Constraints

- 回调/ISR：只 ingress（atomic + pending）；禁止 `printk` / sleep / 刷屏 / Settings
- 不改 `common/event.h`；不用公共 `EVT_ID_*`
- Settings：零写入；不绑 SN/MAC
- 引脚：P1.02 NFC1 / P1.03 NFC2；overlay 开 `&nfct`
- 命名：`nfc_board_bringup()` / `nfc_board_poll()`；新文件仅 `board/nfc_board.c` + `.h`
- NDEF Text 固定：`ink_panel NFC ok`（UTF-8，lang `en`）
- 串口文案：`nfc: bringup ok` / `nfc: bringup fail err=%d` / `nfc: field on|off` / `nfc: data read`
- 提交：仅在用户明确要求时 `git commit`

## Review Focus

- T2T 回调里打日志 → 必须只 atomic；日志只在 `nfc_board_poll`
- `&nfct` 仍 disabled → bringup 失败或无 field；overlay 须 `okay`
- `CONFIG_INK_PANEL_NFC=n` 仍链 NFC 符号 → CMake/`IS_ENABLED` 须对称跳过
- Zigbee 与 NFCT 同开编译失败 → 以 `build` 为准，必要时查 TIMER/外设冲突
- 手机读到旧/空 payload → `payload_set` 须在 `emulation_start` 之前且 encode 成功

---

### Task 1: Kconfig / DT / 构建门控

**Files:**
- Modify: `projects/ink_panel/Kconfig`
- Modify: `projects/ink_panel/prj.conf`
- Modify: `projects/ink_panel/board/app.overlay`（`&nfct` → `okay`）
- Modify: `projects/ink_panel/CMakeLists.txt`

**Interfaces:**
- Produces: `CONFIG_INK_PANEL_NFC`（bool，default y）
- Produces: prj 启用 `CONFIG_NFC_T2T_NRFXLIB`、`CONFIG_NFC_NDEF`、`CONFIG_NFC_NDEF_MSG`、`CONFIG_NFC_NDEF_RECORD`、`CONFIG_NFC_NDEF_TEXT_RECORD`
- Produces: CMake 在 `CONFIG_INK_PANEL_NFC` 时编入 `board/nfc_board.c`（本 task 可先加占位空实现，或与 Task 2 同批）

- [x] **Step 1:** 在 `Kconfig` 增加 `INK_PANEL_NFC`（仿 `INK_PANEL_EPD`）
- [x] **Step 2:** `prj.conf` 增加上述 NFC/NDEF 与 `CONFIG_INK_PANEL_NFC=y`
- [x] **Step 3:** `app.overlay` 将 `&nfct` 改为 `status = "okay"`
- [x] **Step 4:** CMake `if(CONFIG_INK_PANEL_NFC)` 编入 `board/nfc_board.c`（文件在 Task 2 创建）

---

### Task 2: `nfc_board` 板级模块

**Files:**
- Create: `projects/ink_panel/board/nfc_board.h`
- Create: `projects/ink_panel/board/nfc_board.c`

**Interfaces:**
- Produces:
  - `status_t nfc_board_bringup(void);` — setup → encode Text → payload_set → emulation_start；成功 `STATUS_OK`
  - `void nfc_board_poll(void);` — 消费三类 atomic 差量并 `printk`
- Consumes: `nfc_t2t_setup` / `nfc_t2t_payload_set` / `nfc_t2t_emulation_start`；NDEF Text macros（对齐 NCS `samples/nfc/record_text`）
- Payload bytes: `"ink_panel NFC ok"`；lang code: `'e','n'`

- [x] **Step 1:** 写 `.h` 声明（含 include guard `INK_PANEL_NFC_BOARD_H`）
- [x] **Step 2:** 实现回调：`FIELD_ON` / `FIELD_OFF` / `DATA_READ` 各 `atomic_inc`；无日志
- [x] **Step 3:** 实现 `nfc_board_bringup`：失败路径 `printk("nfc: bringup fail err=%d\n", err)` 并返回 `STATUS_FAIL`；成功 `nfc: bringup ok`
- [x] **Step 4:** 实现 `nfc_board_poll`：对三类计数 `atomic_set` 清零前打印对应一行（有增量才打）
- [x] **Step 5:** 编译：`.\nrf.cmd -Project ink_panel build`  
  Expected: 成功；无未解析 NFC 符号

---

### Task 3: 接入 `app` + README

**Files:**
- Modify: `projects/ink_panel/src/app.c`
- Modify: `projects/ink_panel/README.md`
- Modify: `docs/superpowers/specs/2026-11-16-ink-panel-nfc-smoke-design.md`（状态 → 实现中/已实现）

**Interfaces:**
- Consumes: Task 2 的 `nfc_board_bringup` / `nfc_board_poll`
- 模式：`#if IS_ENABLED(CONFIG_INK_PANEL_NFC)` 仿 EPD

- [x] **Step 1:** `app_init`：EPD 块附近调用 `nfc_board_bringup()`（失败只打日志，不阻断 Zigbee）
- [x] **Step 2:** `app_run` 循环内调用 `nfc_board_poll()`
- [x] **Step 3:** README 增加 NFC 行：门控、`nfc: bringup ok`、手机读 `ink_panel NFC ok`
- [x] **Step 4:** 再编译一次确认链接通过

---

### Task 4: 板上验收（人工）

- [ ] 烧录：`.\nrf.cmd -Project ink_panel flash`
- [ ] 串口见 `nfc: bringup ok`
- [ ] 手机靠近 FPC2：`nfc: field on` → 读标签 `nfc: data read` → 移开 `nfc: field off`
- [ ] 手机显示 Text：`ink_panel NFC ok`
- [ ] BTN1 短按仍出现 `app: BTN1 short -> steer` / `zigbee: steering`
- [ ] （可选）`CONFIG_INK_PANEL_NFC=n` 重编：无 `nfc:` 日志且编译通过
