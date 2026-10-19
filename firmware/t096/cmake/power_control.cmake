# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
# Separate awake-only receiver image. No normal GM owner or role dispatcher.
if(NOT CONFIG_BT OR R2_DC_PROFILE OR R2_RC_PROFILE)
 message(FATAL_ERROR "PC requires its isolated BLE leaf configuration")
endif()
set(R2_RC_PROFILE ON) # Reuse pinned capture/USB patches, never the role dispatcher.
foreach(pair R2_SOURCE_SHA R2_CHILD_COMMIT)
 if(NOT ${pair} MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Use the identity-bound public builder")
 endif()
endforeach()
file(SHA256 "${R2_ENROLLMENT_HEADER}" R2_PRIVATE_CONFIG_SHA)
file(SHA256 "${PROJECT_BINARY_DIR}/zephyr/.config" R2_KERNEL_CONFIG_SHA)
string(SHA256 R2_WATCHDOG_BINDING "${R2_SOURCE_SHA}:${R2_PRIVATE_CONFIG_SHA}:${R2_KERNEL_CONFIG_SHA}")
configure_file(include/build_identity.h.in ${CMAKE_CURRENT_BINARY_DIR}/build_identity.h @ONLY)
target_include_directories(app PRIVATE include protocol ${CMAKE_CURRENT_BINARY_DIR}
 ${ZEPHYR_BASE}/subsys/bluetooth/controller/ll_sw/nordic/hal/nrf5/radio)
target_compile_definitions(app PRIVATE R2_PC_PROFILE=1 R2_RC_PROFILE=1 R2_SWITCHABLE_FIELD=1
 R2_NATIVE_BLE=1 R2_USB_SOURCE_STREAM=1 R2_USB_SOF_PROBE=1 R2_WATCHDOG=1 PTOB_NAME_REGISTRY=1)
if(R2_PC_AUTO_PARENT)
 target_compile_definitions(app PRIVATE R2_PC_AUTO_PARENT=1)
endif()
set_source_files_properties(src/rc_dynamic_service.c PROPERTIES COMPILE_DEFINITIONS "R2_DYNAMIC_BACKEND=1;R2_RC_DYNAMIC=1")
set_source_files_properties(src/rc_shadow.c PROPERTIES COMPILE_DEFINITIONS R2_SHADOW_FIELD_MAP=1)
target_sources(app PRIVATE
 src/main.c src/board.c src/console.c src/core.c src/platform.c src/clock.c src/nmea.c
 src/time_service.c src/gnss.c src/ui_render.c src/ui_service.c src/display.c src/timing_events.c src/initiator_diag.c
 src/precision.c src/capture.c src/radio_capture.c src/radio_audit.c src/rc_boundary.c src/rc_shadow.c
 src/rc_dynamic_service.c src/rc_slots.c src/rc_unicast.c
 src/pc_profile_service.c src/pc_experiment.c src/pc_control.c
 src/usb_source.c src/usb_reference_interface.c src/usb_sof_probe.c src/usb_sof_observer.c
 src/watchdog.c src/watchdog_policy.c src/diagnostic.c src/health.c
 protocol/precision_codec.c protocol/rc_dynamic.c protocol/unicast_time.c protocol/relay_codec.c
 protocol/group_join.c protocol/group_auth.c protocol/group_control.c protocol/group_precision.c protocol/role_announcement.c
 protocol/ble_transport.c protocol/network.c protocol/authority_codec.c)
zephyr_link_libraries(-Wl,--undefined=r2_uc_contract)
configure_file(${R2_ENROLLMENT_HEADER} ${CMAKE_CURRENT_BINARY_DIR}/r2_enrollment.h COPYONLY)
include(cmake/usb_ep0_recovery.cmake)
include(cmake/clock_handoff.cmake)
include(cmake/ticker_backport.cmake)
include(cmake/radio_capture.cmake)
include(cmake/precision_schedule.cmake)
target_compile_options(app PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
