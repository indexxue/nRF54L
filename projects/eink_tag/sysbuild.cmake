# SPDX-License-Identifier: Apache-2.0
#
# Build cpuapp (Cortex-M33) + optional MCUBoot + remote cpuflpr (RISC-V FLPR).

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  include(${CMAKE_CURRENT_LIST_DIR}/../../bootloader/sysbuild_include.cmake)
endif()

if("${SB_CONFIG_REMOTE_BOARD}" STREQUAL "")
  message(FATAL_ERROR
    "REMOTE_BOARD must be set (see sysbuild.conf / Kconfig.sysbuild). "
    "Expected e.g. nrf54l15dk/nrf54l15/cpuflpr")
endif()

ExternalZephyrProject_Add(
  APPLICATION remote
  SOURCE_DIR ${APP_DIR}/remote
  BOARD ${SB_CONFIG_REMOTE_BOARD}
  BOARD_REVISION ${BOARD_REVISION}
)

# Place FLPR image in the CPUFLPR Partition Manager domain (RRAM @ 0x165000).
if(SB_CONFIG_SOC_SERIES_NRF54LX)
  set_property(GLOBAL APPEND PROPERTY PM_DOMAINS CPUFLPR)
  set_property(GLOBAL APPEND PROPERTY PM_CPUFLPR_IMAGES remote)
  set_property(GLOBAL PROPERTY DOMAIN_APP_CPUFLPR remote)
  set(CPUFLPR_PM_DOMAIN_DYNAMIC_PARTITION remote CACHE INTERNAL "")
endif()

add_dependencies(${DEFAULT_IMAGE} remote)
sysbuild_add_dependencies(FLASH ${DEFAULT_IMAGE} remote)
