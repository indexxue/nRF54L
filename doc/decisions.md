# Skeleton decisions

Framework choices. **P0 closed** (folders + SDK path + build). Flash/hw verify still deferred.

| Topic | Choice |
|-------|--------|
| P0 status | Accepted 2026-08-30 (`verify_env` / `detect` / `build`) |
| Current focus | App layout + private `lib/`; next product logic or `boards/` |
| Deferred | On-target **flash** and hardware log verify (SWD/UART smoke) |
| Scope (CLI) | `build` / `detect` / `fullclean` primary; `flash` kept but not required now |
| New app | `scripts/new_project.ps1` copies `projects/demo` (includes empty `lib/`) |
| App lifecycle | Shared `start/` (`main` / `start_boot` / `start_poll`) + per-app `src/app.c` (`app_init` → `app_run`); drivers stay in `lib/` / `source/` |
| Start module | Repo `start/` via `modules/nrf54_start`; optional `CONFIG_START_REQUEST_HFXO` |
| Firmware archive | `nrf release <ver>` → elf/hex/bin; `CONFIG_APP_BUILD_RELEASE` makes `log_output()` in `common/log.c` a no-op |
| Lib CMake | Explicit `target_sources` / `target_include_directories` (no glob) |
| Shared code | Repo `common/` via `modules/nrf54_common` |
| Hardware | **Custom PCB** (primary); DK board *name* interim Zephyr target only |
| Debug/flash (later) | External **J-Link via SWD** |
| Console (later) | Separate UART + external serial tool (no `nrf monitor`) |
| SDK | Windows native NCS 3.2.x at `D:\ncs\v3.2.x` (not in Git, not WSL) |
| Layout | `projects/` + `common/` + `modules/` + `boards/` + `tools/` + `firmware/` |
| First app | `projects/demo` as template; `factory` = 厂测 skeleton |
| Boot / OTA | Layout locked for MCUBoot dual-slot; **MCUBoot not enabled in v1 builds** |
| RRAM layout | Boot 64 + slot0 664 + slot1 664 + storage 36 KB (= 1428 KB); FLPR 96 KB reserved — see [`flash_layout.md`](flash_layout.md) |
| Common NVS | Zephyr Settings; keys did/hid/mac/sn/dtype/bmode (N2) — `common/nvs` |
| device_profile | demo + factory stub profiles in `common/device_profile` |
| Factory image | Shares **slot1** (`image-1`) with OTA staging |
| Factory entry | Storage/NVS flag (e.g. `boot_mode=factory`); Boot jumps to slot1 |
| OTA swap | Move/offset style; **no** scratch partition |
| Image sign | Optional in development; enforce later for production |
| Write rules | Running APP writes only slot1 + storage; Boot alone activates slots |
| `pm_static.yml` | Pre-MCUBoot: `app` + `settings_storage` in each project; dual-slot map deferred |
| cbb | Git submodule `cbb/` → `git@github.com:indexxue/cbb.git`（驱动接入仍按需） |
| CLI | `nrf.cmd` / `nrf.ps1` + `nrf.env` |
| Setup | Semi-auto `scripts/setup_env.ps1` via nrfutil sdk-manager |

See [`plan.md`](plan.md) for the living roadmap, [`flash_layout.md`](flash_layout.md) for the address map. Per-app `lib/` detail: `projects/*/lib/README.md`.
