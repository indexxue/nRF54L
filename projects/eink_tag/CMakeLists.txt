# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common)

set(DTC_OVERLAY_FILE ${CMAKE_CURRENT_SOURCE_DIR}/board/app.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(eink_tag)

target_sources(app PRIVATE
  src/main.c
  src/app.c
  board/board_config.c
  # source/epd.c — EPD/SPI 暂未接线，接入后再加
)

target_include_directories(app PRIVATE
  src
  source
)
