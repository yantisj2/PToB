# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
# A separate image: no native BLE, mesh, group enrollment or runtime role owner.
if(CONFIG_BT OR R2_RC_PROFILE OR R2_NATIVE_BLE OR R2_SWITCHABLE_FIELD)
 message(FATAL_ERROR "DC cannot contain Bluetooth or switchable mesh owners")
endif()
foreach(pair R2_SOURCE_SHA R2_CHILD_COMMIT)
 if(NOT ${pair} MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Use the identity-bound public builder")
 endif()
endforeach()
file(SHA256 "${R2_ENROLLMENT_HEADER}" R2_PRIVATE_CONFIG_SHA)
file(SHA256 "${PROJECT_BINARY_DIR}/zephyr/.config" R2_KERNEL_CONFIG_SHA)
string(SHA256 R2_WATCHDOG_BINDING "${R2_SOURCE_SHA}:${R2_PRIVATE_CONFIG_SHA}:${R2_KERNEL_CONFIG_SHA}")
configure_file(include/build_identity.h.in ${CMAKE_CURRENT_BINARY_DIR}/build_identity.h @ONLY)
target_include_directories(app PRIVATE include protocol ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(app PRIVATE R2_DC_PROFILE=1 R2_USB_SOURCE_STREAM=1 R2_USB_SOF_PROBE=1 R2_WATCHDOG=1 PTOB_NAME_REGISTRY=1)
target_sources(app PRIVATE
 src/main.c src/board.c src/console.c src/core.c src/dc_platform.c
 src/clock.c src/nmea.c src/time_service.c src/gnss.c src/precision.c src/capture.c
 src/ui_render.c src/ui_service.c src/display.c src/rc_shadow.c src/rc_boundary.c
 src/usb_source.c src/usb_sof_probe.c src/usb_sof_observer.c src/usb_reference_interface.c
 src/dc_clock.c src/direct_connect.c src/health.c src/watchdog.c src/watchdog_policy.c src/timing_events.c)
target_sources(app PRIVATE src/dc_gnss.c src/dc_gnss_transport.c)
include(cmake/clock_handoff.cmake)
include(cmake/usb_ep0_recovery.cmake)
target_compile_options(app PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
