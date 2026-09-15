# bootloader/

Shared **MCUBoot** configuration for this repo (NCS sysbuild child image).  
Not a standalone `nrf.cmd -Project` app — SDK builds MCUBoot; this directory supplies Kconfig, partition map, and `boot_share` constants.

Behavior contract: [`doc/flash_layout.md`](../doc/flash_layout.md).

## Layout

| Path | Role |
|------|------|
| `prj.conf` | Shared MCUBoot Kconfig (64 KB boot, unsigned, `BOOT_GO_HOOKS`) |
| `prj_swap_move.conf` | Swap-move mode（demo / 量产 APP） |
| `prj_direct_xip.conf` | Direct XIP mode（factory 编 secondary 变体） |
| `boards/nrf54l15dk_nrf54l15_cpuapp.conf` | DK cpuapp board fragment (LFCLK RC, no GRTC) |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | UART20 TX=P1.09 RX=P1.10 (same as apps) |
| `pm_static.yml` | Dual-slot + secondary_app span + `settings` + `boot_share` |
| `include/boot_share.h` | Bypass page address / magic / `boot_mode` record |
| `hooks/` | Zephyr module: `boot_go_hook` + `cleanup_compat` stub |
| `sysbuild_include.cmake` | Include from app `sysbuild.cmake`；`NRF54_MCUBOOT_MODE_CONF` 选 swap/xip |

## This stage vs next

| Done now | Not yet |
|----------|---------|
| Directory + shared fragments | BLE OTA write / delayed confirm |
| Dual-slot `pm_static.yml`（含 secondary_app） | `eink_tag` MCUBoot |
| `boot_share` + `boot_go_hook` | — |
| **`demo` swap-move Boot** + **`factory` Direct XIP secondary** | 板上 factory→secondary 勾验收 |

## How an app opts in

1. `sysbuild.conf`: `SB_CONFIG_BOOTLOADER_MCUBOOT=y` (+ `SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE=y` for development)
2. `sysbuild.cmake`:  
   `include(${CMAKE_CURRENT_LIST_DIR}/../../bootloader/sysbuild_include.cmake)`
3. Replace the app `pm_static.yml` with [`pm_static.yml`](pm_static.yml) (keep in sync; ASCII comments only — Windows PM YAML is GBK-sensitive)
4. Pilot: **`factory`** (done). `demo` FLPR coexistence later.

```powershell
.\nrf.cmd -Project factory fullclean
.\nrf.cmd -Project factory build-all
.\nrf.cmd -Project factory flash
```

`factory flash` programs **slot1 only** with **Direct-XIP secondary** (`mcuboot_secondary_app`). For Boot + slot0 use the production app (`demo flash`).

## Notes

- Development uses `CONFIG_BOOT_SIGNATURE_TYPE_NONE`; production signing stays out of Git (see flash_layout).
- TY rev_a has **no LFXO**: force `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y` (DK default XTAL hangs before UART).
- **No GRTC / no MULTITHREADING in MCUBoot** on this board: with GRTC enabled, Boot could print `Jumping` then hang in `sys_clock_elapsed` and never chainload.
- `CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL=n` + `hooks/cleanup_compat.c` avoids UARTE teardown before jump (and satisfies `nrf_crypto_keys_housekeeping` link).
- `boot_share` factory→secondary：`boot_go_hook`；factory 须烧 Direct-XIP secondary 变体。
