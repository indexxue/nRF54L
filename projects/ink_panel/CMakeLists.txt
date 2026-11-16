# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Zigbee R23 add-on (ncs-zigbee). Set NCS_ZIGBEE_ROOT in nrf.env.
if(DEFINED ENV{NCS_ZIGBEE_ROOT} AND EXISTS "$ENV{NCS_ZIGBEE_ROOT}/zephyr/module.yml")
  list(APPEND EXTRA_ZEPHYR_MODULES "$ENV{NCS_ZIGBEE_ROOT}")
else()
  message(FATAL_ERROR
    "NCS_ZIGBEE_ROOT missing or invalid. Clone Zigbee R23 add-on, e.g.:\n"
    "  git clone --branch v1.4.0 https://github.com/nrfconnect/ncs-zigbee.git D:/ncs/tmp/ncs-zigbee\n"
    "  then set NCS_ZIGBEE_ROOT=D:/ncs/tmp/ncs-zigbee in nrf.env")
endif()

list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
)

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ink_panel)

set(CBB_NPM1300_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/npm1300)
set(CBB_CAP1203_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/cap1203)
set(CBB_EPD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/epd)

target_sources(app PRIVATE
  src/app.c
  board/board_config.c
  board/npm1300_board.c
  board/cap1203_board.c
  lib/zigbee/zigbee_app.c
  lib/zigbee/zboss_mem_globals.c
  ${CBB_NPM1300_DIR}/npm1300.c
  ${CBB_CAP1203_DIR}/cap1203.c
)

if(CONFIG_INK_PANEL_EPD)
  target_sources(app PRIVATE
    board/epd_board.c
    ${CBB_EPD_DIR}/epd.c
    ${CBB_EPD_DIR}/epd_display.c
    ${CBB_EPD_DIR}/epd_display_assets.c
    ${CBB_EPD_DIR}/panels/epd_panel_ws_4in2b_v2.c
  )
endif()

if(CONFIG_INK_PANEL_NFC)
  target_sources(app PRIVATE board/nfc_board.c)
endif()

set_source_files_properties(lib/zigbee/zboss_mem_globals.c PROPERTIES COMPILE_OPTIONS -fno-lto)

target_include_directories(app PRIVATE
  src
  board
  lib/zigbee
  ${CBB_NPM1300_DIR}
  ${CBB_CAP1203_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

if(CONFIG_INK_PANEL_EPD)
  target_include_directories(app PRIVATE ${CBB_EPD_DIR})
endif()
