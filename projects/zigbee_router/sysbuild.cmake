# SPDX-License-Identifier: Apache-2.0
#
# Pull shared MCUBoot fragments (swap-move) when SB_CONFIG_BOOTLOADER_MCUBOOT=y.

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  include(${CMAKE_CURRENT_LIST_DIR}/../../bootloader/sysbuild_include.cmake)
endif()
