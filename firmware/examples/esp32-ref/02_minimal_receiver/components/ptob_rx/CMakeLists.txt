# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
# The copy recipe preserves these two small, shared directories next to ptob_rx.
set(CLOCK "${CMAKE_CURRENT_LIST_DIR}/../ref_clock")
set(PORT "${CMAKE_CURRENT_LIST_DIR}/../esp32s3")
if(NOT EXISTS "${CLOCK}/ref_clock.c")
    set(CLOCK "${CMAKE_CURRENT_LIST_DIR}/../../../components/ref_clock")
    set(PORT "${CMAKE_CURRENT_LIST_DIR}/../../../01_wireless_softpll/ports/esp32s3")
endif()
set(PTOB_TIER 0 CACHE STRING "0 basic, 1 refined, 2 fine descriptor")
if(NOT PTOB_TIER MATCHES "^[012]$")
    message(FATAL_ERROR "PTOB_TIER must be 0, 1, or 2")
endif()
file(SHA256 "$ENV{IDF_PATH}/components/bt/controller/lib_esp32c3_family/esp32s3/libbtdm_app.a" CONTROLLER_HASH)
if(NOT CONTROLLER_HASH STREQUAL "513488f088f7806c5a815580fd00e189265962a8824091b1e60618b7f345d0c1")
    message(FATAL_ERROR "ESP32-REF 02 requires the ESP-IDF v5.5.5 S3 controller pin")
endif()
set(COUNTER_HZ 20000000)
if(PTOB_TIER EQUAL 2)
    file(SHA256 "$ENV{IDF_PATH}/components/esp_driver_mcpwm/src/mcpwm_private.h" MCPWM_HASH)
    if(NOT MCPWM_HASH STREQUAL "4513dc83f66bcdd96be518114f98cab813a5c4f31604997453c5df5123ae213e")
        message(FATAL_ERROR "Fine tier requires the pinned MCPWM driver layout")
    endif()
    set(COUNTER_HZ 80000000)
endif()
idf_component_register(
    SRCS "ptob_rx.c" "direct_wire.c" "receiver_clock.c" "receive_stamp.c" "rx_nimble.c" "join_nimble.c"
         "${CLOCK}/ref_clock.c" "${PORT}/counter.c" "${PORT}/radio_math.c"
    INCLUDE_DIRS "."
    PRIV_INCLUDE_DIRS "${CLOCK}" "${PORT}"
    PRIV_REQUIRES bt mbedtls esp_driver_gptimer esp_driver_mcpwm esp_timer esp_hw_support)
target_compile_options(${COMPONENT_LIB} PRIVATE -Os -Wall -Wextra -Werror -fstack-usage)
target_compile_definitions(${COMPONENT_LIB} PRIVATE REF_CLOCK_HZ=${COUNTER_HZ} PTOB_TIER=${PTOB_TIER})
if(PTOB_TIER EQUAL 2)
    target_include_directories(${COMPONENT_LIB} PRIVATE "$ENV{IDF_PATH}/components/esp_driver_mcpwm/src")
endif()
