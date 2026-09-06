# start/

Public platform bring-up shared by every app under `projects/`.

| File | Role |
|------|------|
| `src/main.c` | Sole `main`: `start_boot` → `app_init` → `app_run` |
| `src/start.c` | `start_boot` / `start_poll` |
| `include/start.h` | API + required app hooks |

Zephyr packaging: [`modules/nrf54_start`](../modules/nrf54_start).

## Boot order

1. `log_init` / log level  
2. `event_init` / `nvs_init`  
3. `board_init`  
4. Optional HFXO (`CONFIG_START_REQUEST_HFXO`)  
5. Banner (`ver` / `product` / `board`)  
6. Mask-gated: LED bootup / `button_init` / `input_init`  
7. **`app_init()`** (project)  
8. **`app_run()`** — call `start_poll()` each tick  

## App CMake

```cmake
list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
)
# do NOT add src/main.c — it comes from this module
target_sources(app PRIVATE src/app.c board/board_config.c)
```

## Kconfig

| Symbol | Default | Meaning |
|--------|---------|---------|
| `CONFIG_START_REQUEST_HFXO` | `n` | Keep external 32 MHz HFXO on after boot (RF apps) |
