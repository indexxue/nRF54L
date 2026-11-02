# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_ble
)

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(eink_tag)

set(CBB_EPD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/epd)
set(CBB_PN5180_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/pn5180)

target_sources(app PRIVATE
  app.c
  board/board_config.c
  source/epd.c
  source/pn5180_port.c
  ${CBB_EPD_DIR}/epd.c
  ${CBB_EPD_DIR}/epd_display.c
  ${CBB_EPD_DIR}/epd_display_assets.c
  ${CBB_EPD_DIR}/panels/epd_panel_gdey029z95.c
  ${CBB_EPD_DIR}/panels/epd_panel_ws_4in2b_v2.c
  ${CBB_PN5180_DIR}/pn5180.c
)

target_include_directories(app PRIVATE
  ${CBB_EPD_DIR}
  ${CBB_PN5180_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}
  source
)
