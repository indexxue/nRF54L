# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Zigbee R23 add-on (ncs-zigbee). Set NCS_ZIGBEE_ROOT in nrf.env.
if(DEFINED ENV{NCS_ZIGBEE_ROOT} AND EXISTS "$ENV{NCS_ZIGBEE_ROOT}/zephyr/module.yml")
  list(APPEND EXTRA_ZEPHYR_MODULES "$ENV{NCS_ZIGBEE_ROOT}")
else()
  message(FATAL_ERROR
    "NCS_ZIGBEE_ROOT missing or invalid. Set NCS_ZIGBEE_ROOT in nrf.env (see zigbee_ed).")
endif()

list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
)

# Per-project pinmux; before find_package(Zephyr)
set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(factory)

target_sources(app PRIVATE
  src/app.c
  board/board_config.c
  lib/cmd/cmd.c
  lib/zigbee/zbft.c
  lib/zigbee/zboss_mem_globals.c
)

set_source_files_properties(lib/zigbee/zboss_mem_globals.c PROPERTIES COMPILE_OPTIONS -fno-lto)

target_include_directories(app PRIVATE
  src
  lib/cmd
  lib/zigbee
)
