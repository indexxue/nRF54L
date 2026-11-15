# nPM1300 cbb Thin Driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an MCU-agnostic nPM1300 A-scope driver under `cbb/npm1300/` with callback-injected I2C, probe-only init, and host mock tests.

**Architecture:** Self-written thin driver (not vendoring `npmx`). 16-bit big-endian register addresses + 8-bit data over I2C `0x6B`. Public API matches the approved spec; board GPIO rail gating stays outside this library.

**Tech Stack:** C11, `stdint`/`stdbool`, host `gcc` mock tests; no Zephyr/HAL inside `cbb`.

**Spec:** `docs/superpowers/specs/2026-11-15-npm1300-cbb-driver-design.md`

## Global Constraints

- Commit driver sources in the **`cbb` submodule** (`git@github.com:indexxue/cbb.git`); main repo only updates submodule pointer + this plan/spec if needed
- Do **not** copy Nordic `adk/npm1300.h` into `cbb` (ADK license restricts redistribution); hand-write only the `#define`s needed for A scope, values verified against PS / npmx ADK
- Follow `cbb/README.md`: callback inject, 7-bit addr, `npm1300_*` naming, no HAL/`Common` includes, no `printf`/`LOG_*`
- `npm1300_init` must not write rails / charger / ship
- Voltage/current: **strict exact table match** → else `NPM1300_ERROR_RANGE` (no silent nearest)
- I2C default 7-bit address `0x6B`

## Review Focus

- `buck_set_vout_mv` without also selecting software VOUT control → rail stays on VSET pin; expect SWCTRL write before/with NORMVOUT
- `buck_enable(BUCK1, false)` while MCU on BUCK1 → board brownout; API still returns OK if bus OK; README must warn
- LDSW I2C enable while MCU GPIO also drives same channel → dual-control fight; README must say product path uses GPIO
- ICHG code encoding (µA/2000 → MSB/LSB) wrong → charge current not what caller asked; mock test pins 200/300 mA bytes
- `ship_enter` success then further I2C → undefined; callers must not assume post-ship bus

## Register map (locked for implementers)

| Bank | Base |
|------|------|
| MAIN | `0x0000` |
| BCHARGER | `0x0300` |
| BUCK | `0x0400` |
| LDSW | `0x0800` |
| SHIP | `0x0B00` |

| Symbol | Addr | Notes |
|--------|------|-------|
| Probe | `0x0434` `BUCKSTATUS` | read-only; `init` probe |
| BUCK1ENASET/CLR | `0x0400` / `0x0401` | write `0x01` pulse |
| BUCK2ENASET/CLR | `0x0402` / `0x0403` | |
| BUCK1NORMVOUT | `0x0408` | 3.0 V code `0x14` |
| BUCK2NORMVOUT | `0x040A` | |
| BUCKSWCTRLSEL | `0x040F` | bit0/1: SWCTRL=`1` for soft VOUT |
| BUCKSTATUS | `0x0434` | status get |
| LDSW1/2 SET/CLR | `0x0800`–`0x0803` | enable pulses |
| LDSWSTATUS | `0x0804` | |
| LDSW1/2LDOSEL | `0x0808` / `0x0809` | `0`=LoadSW, `1`=LDO |
| LDSW1/2VOUTSEL | `0x080C` / `0x080D` | LDO VOUT codes |
| BCHGENABLESET/CLR | `0x0304` / `0x0305` | |
| BCHGISETMSB/LSB | `0x0308` / `0x0309` | code=`ma*1000/2000`; MSB=`code>>1`, LSB=`code&1` |
| BCHGVTERM | `0x030C` | 4.20 V = `0x08` |
| BCHGCHARGESTATUS | `0x0334` | charger status |
| TASKENTERSHIPMODE | `0x0B02` | write `0x01` |
| EVENTSVBUSIN0SET/CLR | `0x0016` / `0x0017` | minimal event group for A |

BUCK VOUT mV table: `1000 + code*100` for `code` `0…23` (1.0–3.3 V).  
VTERM mV table (exact): 3500,3550,3600,3650,4000,4050,4100,4150,4200,4250,4300,4350,4400,4450 → codes `0…0xD`.  
LDO VOUT: same 0.1 V step table as BUCK for codes used by LDSWxVOUTSEL (verify against PS when coding; unknown mV → `RANGE`).

## File structure

| Path | Responsibility |
|------|----------------|
| `cbb/npm1300/npm1300.h` | Types, enums, public API |
| `cbb/npm1300/npm1300.c` | Bus + all A-scope features |
| `cbb/npm1300/README.md` | Device docs + HAL example + GPIO dual-control warning |
| `cbb/npm1300/test/test_npm1300.c` | Host mock I2C + asserts |
| `cbb/README.md` | Add `npm1300` row to device table |

---

### Task 1: Header + bus + probe-only init

**Files:**
- Create: `cbb/npm1300/npm1300.h`
- Create: `cbb/npm1300/npm1300.c`
- Create: `cbb/npm1300/test/test_npm1300.c`
- Test: host compile/run of `test_npm1300.c`

**Interfaces:**
- Consumes: none
- Produces:
  - `typedef int (*npm1300_i2c_write_t)(uint8_t addr7, const uint8_t *data, uint16_t len);`
  - `typedef int (*npm1300_i2c_write_read_t)(uint8_t addr7, const uint8_t *tx, uint16_t tx_len, uint8_t *rx, uint16_t rx_len);`
  - `typedef void (*npm1300_delay_ms_t)(uint32_t ms);` (optional in cfg)
  - `npm1300_status_t npm1300_init(npm1300_t *dev, const npm1300_config_t *cfg);`
  - `npm1300_status_t npm1300_reg_write(npm1300_t *dev, uint16_t addr, uint8_t val);`
  - `npm1300_status_t npm1300_reg_read(npm1300_t *dev, uint16_t addr, uint8_t *val);`
  - Frame: write `[addr_hi, addr_lo, data…]`; read via `write_read` tx=`[addr_hi,addr_lo]` rx=`data`

- [ ] **Step 1: Write failing host tests**

In `test_npm1300.c`: mock 64 KiB register image; `write`/`write_read` decode 16-bit addr into image. Assert:
1. `npm1300_init(NULL, &cfg)` → `PARAM`
2. `npm1300_init` with good callbacks → `OK`, `dev.initialized==true`, **zero writes** to mock (probe is read-only of `0x0434`)
3. After init, `npm1300_reg_write(dev, 0x0408, 0x14)` stores `0x14` at image[`0x0408`]
4. `npm1300_reg_read` returns that value
5. Bus fail callback → `I2C`, and failed init leaves `initialized==false`

- [ ] **Step 2: Run tests — expect fail (symbols missing)**

```powershell
gcc -std=c11 -Wall -Wextra -I cbb/npm1300 cbb/npm1300/test/test_npm1300.c -o cbb/npm1300/test/test_npm1300.exe
```

Expected: link/compile error for missing `npm1300_*`.

- [ ] **Step 3: Implement `npm1300.h` + bus/`init`/`reg_*` in `npm1300.c`**

`init`: require `write` and `write_read`; default addr `0x6B` if cfg addr `0`; read `0x0434`; on success set `initialized`.

- [ ] **Step 4: Run tests — expect pass**

```powershell
gcc -std=c11 -Wall -Wextra -I cbb/npm1300 cbb/npm1300/npm1300.c cbb/npm1300/test/test_npm1300.c -o cbb/npm1300/test/test_npm1300.exe
.\cbb\npm1300\test\test_npm1300.exe
```

Expected: exit `0`, all asserts pass.

- [ ] **Step 5: Commit in cbb submodule**

```powershell
cd cbb
git add npm1300/npm1300.h npm1300/npm1300.c npm1300/test/test_npm1300.c
git commit -m "feat(npm1300): add I2C bus layer and probe-only init"
```

---

### Task 2: BUCK API

**Files:**
- Modify: `cbb/npm1300/npm1300.h`, `cbb/npm1300/npm1300.c`
- Modify: `cbb/npm1300/test/test_npm1300.c`

**Interfaces:**
- Consumes: Task 1 `init` / `reg_*`
- Produces:
  - `npm1300_status_t npm1300_buck_set_vout_mv(npm1300_t *dev, npm1300_buck_t buck, uint16_t mv);`
  - `npm1300_status_t npm1300_buck_enable(npm1300_t *dev, npm1300_buck_t buck, bool on);`
  - `npm1300_status_t npm1300_buck_get_status(npm1300_t *dev, npm1300_buck_t buck, uint8_t *status);`
  - `set_vout_mv(3000)` on BUCK1 → write `0x0408=0x14` **and** set BUCK1 SWCTRL bit in `0x040F`
  - `set_vout_mv(3010)` → `RANGE`
  - `enable(true)` → pulse `0x0400=0x01`; `enable(false)` → `0x0401=0x01`

- [ ] **Step 1: Add failing tests** for 3000 mV encoding, SWCTRL bit, RANGE on 3010, enable pulses, bad buck index → `PARAM`

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement BUCK helpers + APIs**

- [ ] **Step 4: Run — expect pass**

- [ ] **Step 5: Commit**

```powershell
cd cbb
git add npm1300/npm1300.h npm1300/npm1300.c npm1300/test/test_npm1300.c
git commit -m "feat(npm1300): add BUCK voltage and enable API"
```

---

### Task 3: LDO / Load Switch API

**Files:**
- Modify: `cbb/npm1300/npm1300.h`, `cbb/npm1300/npm1300.c`, `cbb/npm1300/test/test_npm1300.c`

**Interfaces:**
- Produces:
  - `npm1300_status_t npm1300_ldsw_set_mode(npm1300_t *dev, npm1300_ldsw_t idx, npm1300_ldsw_mode_t mode);`
  - `npm1300_status_t npm1300_ldsw_set_vout_mv(npm1300_t *dev, npm1300_ldsw_t idx, uint16_t mv);`
  - `npm1300_status_t npm1300_ldsw_enable(npm1300_t *dev, npm1300_ldsw_t idx, bool on);`
  - `npm1300_status_t npm1300_ldsw_get_status(npm1300_t *dev, npm1300_ldsw_t idx, uint8_t *status);`
  - LDO1 / LDSW2: mode LDO writes `0x0808=1`; LoadSW writes `0`; enable LDSW2 → `0x0802=1` / clr `0x0803=1`
  - `set_vout_mv(LDSW1, 3000)` → `0x080C` code for 3.0 V; invalid mV → `RANGE`

- [ ] **Step 1: Failing tests** for mode, vout, enable pulses, status read of `0x0804`

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement LDSW APIs**

- [ ] **Step 4: Run — expect pass**

- [ ] **Step 5: Commit**

```powershell
cd cbb
git add npm1300/npm1300.h npm1300/npm1300.c npm1300/test/test_npm1300.c
git commit -m "feat(npm1300): add LDO and load-switch API"
```

---

### Task 4: Charger basic API

**Files:**
- Modify: `cbb/npm1300/npm1300.h`, `cbb/npm1300/npm1300.c`, `cbb/npm1300/test/test_npm1300.c`

**Interfaces:**
- Produces:
  - `npm1300_status_t npm1300_charger_set_ichg_ma(npm1300_t *dev, uint16_t ma);`
  - `npm1300_status_t npm1300_charger_set_vterm_mv(npm1300_t *dev, uint16_t mv);`
  - `npm1300_status_t npm1300_charger_enable(npm1300_t *dev, bool on);`
  - `npm1300_status_t npm1300_charger_get_status(npm1300_t *dev, uint8_t *status);`
  - 200 mA → code `100` → MSB `50` (`0x32`), LSB `0` at `0x0308`/`0x0309`
  - 300 mA → code `150` → MSB `75` (`0x4B`), LSB `0`
  - `ma` outside 32–800 or not multiple of 2 → `RANGE`
  - `vterm 4200` → `0x030C=0x08`; `4210` → `RANGE`
  - enable → `0x0304=0x01`; disable → `0x0305` clear path per PS (write enable-clr register)
  - get_status reads `0x0334`

- [ ] **Step 1: Failing tests** for ichg/vterm/enable/status as above

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement charger APIs**

- [ ] **Step 4: Run — expect pass**

- [ ] **Step 5: Commit**

```powershell
cd cbb
git add npm1300/npm1300.h npm1300/npm1300.c npm1300/test/test_npm1300.c
git commit -m "feat(npm1300): add charger current, vterm, enable"
```

---

### Task 5: Ship, events, README, catalog

**Files:**
- Modify: `cbb/npm1300/npm1300.h`, `cbb/npm1300/npm1300.c`, `cbb/npm1300/test/test_npm1300.c`
- Create: `cbb/npm1300/README.md`
- Modify: `cbb/README.md` (device table row)

**Interfaces:**
- Produces:
  - `npm1300_status_t npm1300_ship_enter(npm1300_t *dev);` → write `0x0B02=0x01`
  - `npm1300_status_t npm1300_event_read(npm1300_t *dev, uint8_t *mask);` → read `0x0016`
  - `npm1300_status_t npm1300_event_clear(npm1300_t *dev, uint8_t mask);` → write `0x0017`
  - README: cbb template sections; STM32 HAL `<<1` example; warn BUCK1 disable / GPIO vs I2C dual-control / ship wakeup via SHPHLD
  - `cbb/README.md`: add `| npm1300 | nPM1300 | I2C | PMIC：充电 / BUCK / LDO·LS / Ship |`

- [ ] **Step 1: Failing tests** for ship write, event read/clear

- [ ] **Step 2: Run — expect fail**

- [ ] **Step 3: Implement ship/events + READMEs**

- [ ] **Step 4: Run full host test — expect pass**

- [ ] **Step 5: Commit in cbb; then update main repo submodule pointer**

```powershell
cd cbb
git add npm1300 cbb/README.md
# if README path is repo-root README inside cbb:
git add npm1300 README.md
git commit -m "feat(npm1300): add ship, events, and docs"

cd ..
git add cbb
git commit -m "chore: bump cbb submodule for npm1300 driver"
```

---

## Spec coverage checklist

| Spec item | Task |
|-----------|------|
| Probe-only init | 1 |
| reg_write/read | 1 |
| BUCK set/enable/status | 2 |
| LDSW mode/vout/enable/status | 3 |
| Charger ichg/vterm/enable/status | 4 |
| Ship + events | 5 |
| README + cbb catalog | 5 |
| No npmx vendor / no HAL in cbb | all |
| Strict RANGE | 2–4 tests |

## HW acceptance (manual, post-merge)

Not part of host CI; do on EVT when wiring ready:

1. `init` + `reg_read` OK on I2C0
2. `buck_set_vout_mv(BUCK1, 3000)` → ~3.0 V on VOUT1
3. Single-path LS2/LDO1 toggle visible
4. Charger status readable after configure
5. Ship only in dedicated fixture with SHPHLD wake
