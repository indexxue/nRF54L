# Shared MCUBoot sysbuild helpers for this repository.
#
# From an application sysbuild.cmake (when enabling MCUBoot):
#   include(${CMAKE_CURRENT_LIST_DIR}/../../bootloader/sysbuild_include.cmake)
#
# Optional before include:
#   set(NRF54_MCUBOOT_MODE_CONF "prj_swap_move.conf")   # default
#   set(NRF54_MCUBOOT_MODE_CONF "prj_direct_xip.conf")  # factory secondary XIP
#
# Requires SB_CONFIG_BOOTLOADER_MCUBOOT=y in that application's sysbuild.conf.
# Also replace the app pm_static.yml with bootloader/pm_static.yml (or merge).

get_filename_component(NRF54_BOOTLOADER_DIR "${CMAKE_CURRENT_LIST_DIR}" ABSOLUTE)

if(NOT DEFINED NRF54_MCUBOOT_MODE_CONF OR "${NRF54_MCUBOOT_MODE_CONF}" STREQUAL "")
  set(NRF54_MCUBOOT_MODE_CONF "prj_swap_move.conf")
endif()

set(_nrf54_mcuboot_extra_conf
  "${NRF54_BOOTLOADER_DIR}/prj.conf"
  "${NRF54_BOOTLOADER_DIR}/${NRF54_MCUBOOT_MODE_CONF}"
  "${NRF54_BOOTLOADER_DIR}/boards/nrf54l15dk_nrf54l15_cpuapp.conf"
)

# Preserve any caller-provided fragments, then append shared ones.
if(DEFINED mcuboot_EXTRA_CONF_FILE AND NOT "${mcuboot_EXTRA_CONF_FILE}" STREQUAL "")
  set(mcuboot_EXTRA_CONF_FILE
    "${mcuboot_EXTRA_CONF_FILE};${_nrf54_mcuboot_extra_conf}"
    CACHE INTERNAL "nRF54 shared mcuboot EXTRA_CONF_FILE" FORCE)
else()
  set(mcuboot_EXTRA_CONF_FILE "${_nrf54_mcuboot_extra_conf}"
    CACHE INTERNAL "nRF54 shared mcuboot EXTRA_CONF_FILE" FORCE)
endif()

unset(_nrf54_mcuboot_extra_conf)

# Factory boot_share hook module (boot_go_hook).
set(mcuboot_EXTRA_ZEPHYR_MODULES "${NRF54_BOOTLOADER_DIR}/hooks"
  CACHE INTERNAL "nRF54 mcuboot hooks module" FORCE)

# Same UART pins as apps (P1.09/P1.10).
set(mcuboot_EXTRA_DTC_OVERLAY_FILE
  "${NRF54_BOOTLOADER_DIR}/boards/nrf54l15dk_nrf54l15_cpuapp.overlay"
  CACHE INTERNAL "nRF54 mcuboot uart overlay" FORCE)
