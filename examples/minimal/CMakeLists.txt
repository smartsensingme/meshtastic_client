# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
get_filename_component(MESHTASTIC_CLIENT_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." REALPATH)
set(RT_LOG_MODULE_DIR "${MESHTASTIC_CLIENT_DIR}/../rt_log" CACHE PATH "rt_log repository")
list(APPEND ZEPHYR_EXTRA_MODULES "${MESHTASTIC_CLIENT_DIR}" "${RT_LOG_MODULE_DIR}")
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(meshtastic_minimal)
target_sources(app PRIVATE src/main.c)
