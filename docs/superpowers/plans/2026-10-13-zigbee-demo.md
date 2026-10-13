# zigbee_demo Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship `projects/zigbee_demo` as a Zigbee End Device that joins ZigBridge-EM on channel 11, with LED/button UX and no BLE/EPD.

**Architecture:** Scaffold from `projects/demo`, board pins from `eink_tag` rev_b (UART/LED/BTN only). App owns state machine; Zigbee/button callbacks only enqueue. Stack = Zigbee R23 add-on ZBOSS ED + BDB steering/leave (Light Switch sample minus ZCL).

**Tech Stack:** NCS 3.2.x (or add-on-compatible NCS), Zephyr, Zigbee R23 add-on, `nrf54_common` / `nrf54_start`, eink_tag rev_b GPIO.

**Spec:** `docs/superpowers/specs/2026-10-13-zigbee-demo-design.md`

## Global Constraints

- Project name: `zigbee_demo` under `projects/`
- Role: Zigbee End Device only; no Coordinator; no Sleepy ED this phase
- Channel must include **11**
- No `nrf54_ble`, no EPD/SPI/PN5180, no OnOff/Identify ZCL
- Do not modify `eink_tag` / `demo` / `factory` production paths except read-only reuse of pin tables
- Naming: `module_verb_object()`; callbacks ingress-only (`K_NO_WAIT` enqueue)
- Board: eink_tag rev_b — LED P2.08/P2.09, BTN1 P0.00, BTN2 P0.01, UART P1.04/P1.05

## Review Focus

- Steering while already JOINED: expect ignore or re-steer per stack; must not crash; serial logs clearly
- Leave while STEERING: must cancel/leave cleanly back to IDLE
- Zigbee callback context: no sleep / no settings write / no LED blocking work in callback
- Channel misconfig (not including 11): build-time or boot log must make channel 11 visible in README + conf
- Add-on / NCS version mismatch: build failure must be documented in README with exact version pin

## File map

| Path | Role |
|------|------|
| `projects/zigbee_demo/**` | New app (scaffold + customize) |
| `projects/zigbee_demo/board/app.overlay` | UART+LED+BTN×2 from eink_tag (strip SPI/PN5180) |
| `projects/zigbee_demo/board/board_config.c` | LED×2 + BTN×2, no SPI/BLE mask |
| `projects/zigbee_demo/prj.conf` | No BT; Zigbee ED + channel 11 |
| `projects/zigbee_demo/src/app.c` | State machine, button handlers, LED/serial |
| `projects/zigbee_demo/README.md` | Build/flash/join/leave |

---

### Task 1: Scaffold project + board (no Zigbee yet)

**Files:**
- Create: `projects/zigbee_demo/` via `new_project.ps1`
- Modify: `board/app.overlay`, `board/board_config.c`, `prj.conf`, `CMakeLists.txt`, `src/app.c`, `README.md`

**Interfaces:**
- Consumes: `scripts/new_project.ps1`; eink_tag overlay pin nodes for led/btn/uart
- Produces: buildable project with dual buttons + LEDs; `app_init`/`app_run` heartbeat; button events logged

- [ ] **Step 1:** Run `powershell -ExecutionPolicy Bypass -File .\scripts\new_project.ps1 -Name zigbee_demo`
- [ ] **Step 2:** Replace overlay/board_config with eink_tag UART+LED+BTN×2 only (no spi00/spi22/PN5180/BLE)
- [ ] **Step 3:** Keep demo-like `prj.conf` (UART/GPIO/HFXO/NVS); ensure no `CONFIG_BT`
- [ ] **Step 4:** Wire `app.c` to log BTN1 short / BTN2 long via existing button API; LED boot scene unchanged
- [ ] **Step 5:** Build: `.\nrf.cmd -Project zigbee_demo build` — expect SUCCESS
- [ ] **Step 6:** Commit scaffold

---

### Task 2: Integrate Zigbee ED stack (steer/leave)

**Files:**
- Modify: `prj.conf`, `CMakeLists.txt`, `src/app.c`, `README.md`
- Possibly: `sysbuild.conf` / `pm_static.yml` if add-on requires partition changes

**Interfaces:**
- Consumes: Nordic Zigbee R23 add-on APIs (BDB steering, leave, signal handler) — pin exact symbols from installed add-on sample at implement time
- Produces:
  - `zigbee_app_start(void)` from `app_init`
  - `zigbee_app_steer_request(void)` / `zigbee_app_leave_request(void)` (thread-safe from app context)
  - Signal path → msgq → app updates state enum `{IDLE,STEERING,JOINED,FAILED}`

- [ ] **Step 1:** Locate local Zigbee add-on / Light Switch sample; record NCS version in README
- [ ] **Step 2:** Port minimal ED Kconfig + sources (no OnOff/Identify clusters)
- [ ] **Step 3:** Force/include channel **11** in conf; log channel at boot
- [ ] **Step 4:** Map stack joined/failed/left → state + `LOG_INFO` with IEEE/NWK on join
- [ ] **Step 5:** Build — expect SUCCESS (or document version blocker and stop with README note)
- [ ] **Step 6:** Commit Zigbee integration

---

### Task 3: Button + LED UX + README acceptance

**Files:**
- Modify: `src/app.c`, `README.md`

**Interfaces:**
- Consumes: Task 2 steer/leave APIs; `button_*` events; `led_scene_*`
- Produces: BTN1 short→steer; BTN2 long→leave; LED scenes for STEERING/JOINED/FAILED/IDLE

- [ ] **Step 1:** Bind button events in `app_run` / event path (not in ISR beyond existing button ingress)
- [ ] **Step 2:** Map states to LED scenes (闪/常亮/快闪/熄)
- [ ] **Step 3:** README: build, flash, channel 11, BTN map, ZigBridge `znp join on` steps, A1–A5 checklist
- [ ] **Step 4:** Build verify
- [ ] **Step 5:** Commit UX + docs

---

## Spec coverage check

| Spec item | Task |
|-----------|------|
| New project, no BLE path damage | 1 |
| ED + channel 11 + steer/leave | 2 |
| BTN1/BTN2 mapping + LED/serial | 3 |
| README + A1–A5 | 3 |
| No EPD/ZCL/Sleepy | all tasks omit |
