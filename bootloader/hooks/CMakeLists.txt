# SPDX-License-Identifier: Apache-2.0
#
# MCUBoot EXTRA_ZEPHYR_MODULES: factory boot_share → boot from secondary.

zephyr_library()
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)
zephyr_include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../common/include)
zephyr_library_sources(
  boot_go_hook.c
  cleanup_compat.c
)
zephyr_library_link_libraries(MCUBOOT_BOOTUTIL)
