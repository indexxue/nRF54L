# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module wrapper for repo `start/` (shared main + platform boot).

set(NRF54_START_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../start)

zephyr_library_named(nrf54_start)

zephyr_include_directories(${NRF54_START_ROOT}/include)

zephyr_library_sources(
  ${NRF54_START_ROOT}/src/start.c
)

# main must live on app so the linker always keeps the entry point.
target_sources(app PRIVATE ${NRF54_START_ROOT}/src/main.c)
