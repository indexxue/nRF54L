# ink_panel Round-1 Implementation Plan

> **For agentic workers:** Execute task-by-task. Steps use checkbox syntax.

**Goal:** Create `projects/ink_panel` from `zigbee_ed` with EVT netlist UART/LED/BTN1 and Zigbee ED discoverable.

**Architecture:** Whole-tree copy of `zigbee_ed`; retarget `board/app.overlay` + `board_config`; rename Model/strings; keep MCUBoot + OnOff probe.

**Tech Stack:** NCS 3.2 + ncs-zigbee R23, Zephyr, MCUBoot sysbuild

**Spec:** `docs/superpowers/specs/2026-11-12-ink-panel-design.md`

## Global Constraints

- Do not change `zigbee_ed` runtime behavior
- Pins: UART P2.01/P2.00, LED P1.13, BTN1 P0.00; never map P0.01 as BTN2
- Single LED only in `board_config`

## Tasks

### Task 1: Scaffold copy + rename
- [x] Copy `projects/zigbee_ed` → `projects/ink_panel` (skip `*.xlsx`, `.clangd` optional)
- [x] `project(ink_panel)`, Kconfig default Model `ink_panel`, `prj.conf` Model, comments
- [x] Rewrite project README for EVT bring-up

### Task 2: Board EVT pins
- [x] `board/app.overlay`: UART/LED/BTN1/HFXO/timer; no btn2; disable extra LEDs
- [x] `board/board_config.c`: 1 LED, 1 button, name `ink_panel_evt`

### Task 3: App strings + BTN2 note
- [x] `src/app.c`: rename printk/LOG; remove BTN2 handlers (no hardware)
- [x] Root README: add `ink_panel` to project list

### Task 4: Build verify
- [x] `.\nrf.cmd -Project ink_panel build` (or build-all if required)
- [x] Fix compile errors until green
