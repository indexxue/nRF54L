# bootloader/

Shared **MCUBoot** configuration for this repo (NCS sysbuild child image).  
Not a standalone `nrf.cmd -Project` app — SDK builds MCUBoot; this directory supplies Kconfig, partition map, and `boot_share` constants.

Behavior contract: [`doc/flash_layout.md`](../doc/flash_layout.md).

## Layout

| Path | Role |
|------|------|
| `prj.conf` | Shared MCUBoot Kconfig (64 KB boot, swap move, unsigned for development, `BOOT_GO_HOOKS`) |
| `boards/nrf54l15dk_nrf54l15_cpuapp.conf` | DK cpuapp board fragment (LFCLK RC, no GRTC) |
| `boards/nrf54l15dk_nrf54l15_cpuapp.overlay` | UART20 TX=P1.09 RX=P1.10 (same as apps) |
| `pm_static.yml` | Canonical dual-slot + `settings_storage` 32 KB + `boot_share` 4 KB |
| `include/boot_share.h` | Bypass page address / magic / `boot_mode` record |
| `hooks/` | Zephyr module: `boot_go_hook` + `cleanup_compat` stub |
| `sysbuild_include.cmake` | One-line include from an app `sysbuild.cmake` |

## This stage vs next

| Done now | Not yet |
|----------|---------|
| Directory + shared fragments | BLE OTA write path / confirm API |
| Dual-slot `pm_static.yml` | `demo` / `eink_tag` MCUBoot |
| `boot_share.h` + APP dual-write (`nvs_boot_mode_set`) | Delayed confirm helper |
| **`factory` MCUBoot chainload** (Boot → slot0) verified on TY UART | Board smoke of factory → secondary bypass |

## How an app opts in

1. `sysbuild.conf`: `SB_CONFIG_BOOTLOADER_MCUBOOT=y` (+ `SB_CONFIG_BOOT_SIGNATURE_TYPE_NONE=y` for development)
2. `sysbuild.cmake`:  
   `include(${CMAKE_CURRENT_LIST_DIR}/../../bootloader/sysbuild_include.cmake)`
3. Replace the app `pm_static.yml` with [`pm_static.yml`](pm_static.yml) (keep in sync; ASCII comments only — Windows PM YAML is GBK-sensitive)
4. Pilot: **`factory`** (done). `demo` FLPR coexistence later.

```powershell
.\nrf.cmd -Project factory fullclean
.\nrf.cmd -Project factory build
.\nrf.cmd -Project factory flash
```

## Notes

- Development uses `CONFIG_BOOT_SIGNATURE_TYPE_NONE`; production signing stays out of Git (see flash_layout).
- TY rev_a has **no LFXO**: force `CONFIG_CLOCK_CONTROL_NRF_K32SRC_RC=y` (DK default XTAL hangs before UART).
- **No GRTC / no MULTITHREADING in MCUBoot** on this board: with GRTC enabled, Boot could print `Jumping` then hang in `sys_clock_elapsed` and never chainload.
- `CONFIG_MCUBOOT_NRF_CLEANUP_PERIPHERAL=n` + `hooks/cleanup_compat.c` avoids UARTE teardown before jump (and satisfies `nrf_crypto_keys_housekeeping` link).
- `boot_share` factory→secondary path is implemented in `boot_go_hook`; board flag smoke is next.
