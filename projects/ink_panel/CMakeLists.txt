# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common
  ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_start
)

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ink_panel)

set(CBB_NPM1300_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/npm1300)
set(CBB_EPD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../cbb/epd)

target_sources(app PRIVATE
  src/app.c
  board/board_config.c
  board/npm1300_board.c
  ${CBB_NPM1300_DIR}/npm1300.c
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

target_include_directories(app PRIVATE
  src
  board
  ${CBB_NPM1300_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../common/include
)

if(CONFIG_INK_PANEL_EPD)
  target_include_directories(app PRIVATE ${CBB_EPD_DIR})
endif()
