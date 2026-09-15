# SPDX-License-Identifier: Apache-2.0
#
# Pull shared MCUBoot fragments when SB_CONFIG_BOOTLOADER_MCUBOOT=y.
# Direct XIP: build mcuboot_secondary_app linked for slot1 @ 0xB6000.

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  set(NRF54_MCUBOOT_MODE_CONF "prj_direct_xip.conf")
  include(${CMAKE_CURRENT_LIST_DIR}/../../bootloader/sysbuild_include.cmake)
endif()
