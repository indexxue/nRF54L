# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module wrapper for repo `common/` sources.
# Module root is this directory; portable code lives in ../../common.

set(NRF54_COMMON_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../common)

zephyr_library_named(nrf54_common)

zephyr_include_directories(${NRF54_COMMON_ROOT}/include)

zephyr_library_sources(
    ${NRF54_COMMON_ROOT}/src/log.c
    ${NRF54_COMMON_ROOT}/src/fw_version.c
    ${NRF54_COMMON_ROOT}/src/flexible_button.c
    ${NRF54_COMMON_ROOT}/src/crc32.c
    ${NRF54_COMMON_ROOT}/src/event.c
    ${NRF54_COMMON_ROOT}/src/nvs.c
    ${NRF54_COMMON_ROOT}/src/device_profile.c
    ${NRF54_COMMON_ROOT}/src/led_scene.c
    ${NRF54_COMMON_ROOT}/src/board.c
    ${NRF54_COMMON_ROOT}/src/button.c
    ${NRF54_COMMON_ROOT}/src/input.c
)
