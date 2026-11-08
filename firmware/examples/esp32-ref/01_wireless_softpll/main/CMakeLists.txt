# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
set(REF_COMPONENTS "${CMAKE_CURRENT_LIST_DIR}/../../components")
set(REF_CLOCK_HZ 20000000 CACHE STRING "Example clock rate in Hz")
idf_component_register(
    SRCS "main.c" "wireless.c"
         "../ports/esp32s3/radio.c" "../ports/esp32s3/radio_math.c" "../ports/esp32s3/counter.c"
         "../ports/esp32s3/pulse.c"
         "../ports/w12/display.c" "../ports/w12/power.c"
         "${REF_COMPONENTS}/ref_clock/ref_clock.c"
         "${REF_COMPONENTS}/ref_softpll/ref_softpll.c"
         "${REF_COMPONENTS}/ref_protocol/ref_wire.c"
         "${REF_COMPONENTS}/ref_protocol/ref_join.c"
         "${REF_COMPONENTS}/ref_protocol/ref_session.c"
         "${REF_COMPONENTS}/ref_ui/ref_button.c"
         "${REF_COMPONENTS}/ref_ui/ref_shutdown.c"
         "${REF_COMPONENTS}/ref_ui/ref_display.c"
    INCLUDE_DIRS "." "../ports/esp32s3" "../ports/w12"
         "${REF_COMPONENTS}/ref_clock" "${REF_COMPONENTS}/ref_softpll"
         "${REF_COMPONENTS}/ref_protocol" "${REF_COMPONENTS}/ref_ui"
         "${CMAKE_CURRENT_BINARY_DIR}"
    REQUIRES bt mbedtls esp_driver_gptimer esp_driver_gpio esp_driver_i2c
             esp_driver_mcpwm esp_driver_usb_serial_jtag esp_timer esp_hw_support)
configure_file("${REF_LOCAL_HEADER}" "${CMAKE_CURRENT_BINARY_DIR}/ref_local.h" COPYONLY)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wall -Wextra -Werror -fstack-usage)
target_compile_definitions(${COMPONENT_LIB} PRIVATE REF_SOURCE_ID="${REF_SOURCE_ID}")
target_compile_definitions(${COMPONENT_LIB} PRIVATE REF_CLOCK_HZ=${REF_CLOCK_HZ})
if(REF_CLOCK_HZ EQUAL 80000000)
    target_include_directories(${COMPONENT_LIB} PRIVATE "$ENV{IDF_PATH}/components/esp_driver_mcpwm/src")
endif()
