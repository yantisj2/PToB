# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20.0)
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ptob_mhat VERSION 0.5.5)
zephyr_linker_sources(ROM_START SORT_KEY zzz ${CMAKE_CURRENT_SOURCE_DIR}/module_reserve.ld)
set(T096 ${CMAKE_CURRENT_SOURCE_DIR}/../t096)
if(NOT EXISTS "${MHAT_PRIVATE_HEADER}")
 message(FATAL_ERROR "Use tools/build.py with private exact-device enrollment")
endif()
configure_file(${MHAT_PRIVATE_HEADER} ${CMAKE_CURRENT_BINARY_DIR}/mhat_private.h COPYONLY)
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR} ${T096}/include ${T096}/protocol
 ${ZEPHYR_BASE}/subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/radio)
target_compile_definitions(app PRIVATE R2_RC_PROFILE=1 R2_SWITCHABLE_FIELD=1 R2_TZ_WIRE=1
 R2_USB_SOURCE_STREAM=1 R2_NATIVE_BLE=1)
set_source_files_properties(${T096}/src/rc_dynamic_service.c PROPERTIES COMPILE_DEFINITIONS "R2_DYNAMIC_BACKEND=1;R2_RC_DYNAMIC=1")
target_sources(app PRIVATE src/main.c src/mesh.c src/clock_adapter.c src/pps.c src/support.c src/gm_control.c
 ${T096}/src/core.c ${T096}/src/rc_dynamic_service.c ${T096}/src/rc_boundary.c ${T096}/src/rc_slots.c
 ${T096}/src/rc_unicast.c ${T096}/src/radio_capture.c ${T096}/src/radio_audit.c
 ${T096}/src/initiator_diag.c ${T096}/src/timing_events.c
 ${T096}/protocol/rc_dynamic.c ${T096}/protocol/unicast_time.c ${T096}/protocol/relay_codec.c
 ${T096}/protocol/precision_codec.c ${T096}/protocol/group_join.c ${T096}/protocol/group_auth.c
 ${T096}/protocol/group_control.c ${T096}/protocol/group_precision.c
 ${T096}/protocol/role_announcement.c ${T096}/protocol/gm7_codec.c)
# Reuse pinned controller fixes and capture hooks without editing their sources.
include(${T096}/cmake/clock_handoff.cmake)
set(R2_PC_PROFILE ON) # ticker generator only: this target has no T096 crash storage.
include(${T096}/cmake/ticker_backport.cmake)
unset(R2_PC_PROFILE)
include(${T096}/cmake/radio_capture.cmake)
include(${T096}/cmake/precision_schedule.cmake)
include(${T096}/cmake/controller-radio-fixes.cmake)
include(${T096}/cmake/scan-aux-context.cmake)
target_compile_options(app PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
