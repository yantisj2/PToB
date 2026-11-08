# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
set(UI "${CMAKE_CURRENT_LIST_DIR}/../../components/ref_ui")
set(BOARD "${CMAKE_CURRENT_LIST_DIR}/../../01_wireless_softpll/ports/w12")
idf_component_register(
    SRCS "main.c" "screen.c" "${UI}/ref_button.c" "${UI}/ref_shutdown.c" "${UI}/ref_display.c"
         "${BOARD}/display.c" "${BOARD}/power.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${UI}" "${BOARD}" "../../components/ref_clock" "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES ptob_rx bt esp_driver_gpio esp_driver_i2c esp_driver_usb_serial_jtag esp_timer esp_hw_support)
configure_file("${REF_LOCAL_HEADER}" "${CMAKE_CURRENT_BINARY_DIR}/ref_local.h" COPYONLY)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wall -Wextra -Werror -fstack-usage)
target_compile_definitions(${COMPONENT_LIB} PRIVATE PTOB_TIER=${PTOB_TIER} REF_SOURCE_ID="${REF_SOURCE_ID}")
