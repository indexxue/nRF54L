# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# Shared code: ../../common via Zephyr module ../../modules/nrf54_common
list(APPEND EXTRA_ZEPHYR_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/nrf54_common)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(factory)

target_sources(app PRIVATE
  src/main.c
  src/app.c
  lib/cmd/cmd.c
)

target_include_directories(app PRIVATE
  src
  lib/cmd
)
