# SPDX-License-Identifier: Apache-2.0
#
# FLPR (RISC-V) remote image — no start/ / common/; tiny standalone Zephyr app.

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(remote)

target_sources(app PRIVATE src/main.c)
