# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Zigbee R23 add-on (ncs-zigbee). Set NCS_ZIGBEE_ROOT in nrf.env.
if(DEFINED ENV{NCS_ZIGBEE_ROOT} AND EXISTS "$ENV{NCS_ZIGBEE_ROOT}/zephyr/module.yml")
  list(APPEND EXTRA_ZEPHYR_MODULES "$ENV{NCS_ZIGBEE_ROOT}")
else()
  message(FATAL_ERROR
    "NCS_ZIGBEE_ROOT missing or invalid. Clone Zigbee R23 add-on, e.g.:\n"
    "  git clone --branch v1.4.0 https://github.com/nrfconnect/ncs-zigbee.git D:/ncs/tmp/ncs-zigbee\n"
    "  then set NCS_ZIGBEE_ROOT=D:/ncs/tmp/ncs-zigbee in nrf.env\n"
    "NOTE: add-on v1.4.0 targets NCS v3.4.x; this repo default is NCS v3.2.0 ? see README.")
endif()

list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
)

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(zigbee_demo)

target_sources(app PRIVATE
  src/app.c
  board/board_config.c
  lib/zigbee/zigbee_app.c
  lib/zigbee/zboss_mem_globals.c
)

# LTO prebuilt ZBOSS: keep mem globals on classic ELF path (Nordic sample pattern).
set_source_files_properties(lib/zigbee/zboss_mem_globals.c PROPERTIES COMPILE_OPTIONS -fno-lto)

target_include_directories(app PRIVATE
  src
  lib/zigbee
)
