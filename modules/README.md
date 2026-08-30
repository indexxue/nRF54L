# modules/

Zephyr / NCS **module wrappers** (each has `zephyr/module.yml`).

Portable application code stays in [`../common`](../common) — do not put product sources here.

| Module | Path | Sources |
|--------|------|---------|
| `nrf54_common` | `nrf54_common/` | `../../common` |

App CMake (before `find_package(Zephyr)`):

```cmake
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common)
```
