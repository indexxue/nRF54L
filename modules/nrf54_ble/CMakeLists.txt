# SPDX-License-Identifier: Apache-2.0
#
# Zephyr module wrapper for repo `ble/` (sibling of common/).

set(NRF54_BLE_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../ble)

zephyr_library_named(nrf54_ble)

zephyr_include_directories(${NRF54_BLE_ROOT}/include)

zephyr_library_sources(
    ${NRF54_BLE_ROOT}/src/ble.c
    ${NRF54_BLE_ROOT}/src/ble_stack.c
    ${NRF54_BLE_ROOT}/src/ble_gap.c
    ${NRF54_BLE_ROOT}/src/ble_gatt.c
    ${NRF54_BLE_ROOT}/src/ble_evt.c
    ${NRF54_BLE_ROOT}/src/ble_cmd.c
    ${NRF54_BLE_ROOT}/src/ble_cmd_thread.c
)

zephyr_library_include_directories(${NRF54_BLE_ROOT}/src)
