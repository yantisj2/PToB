# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
if(CMAKE_BUILD_EARLY_EXPANSION)
 idf_component_register(REQUIRES M5Unified esp_psram bt mbedtls esp_driver_spi esp_driver_gptimer esp_driver_i2c esp_driver_gpio esp_driver_usb_serial_jtag esp_driver_uart esp_driver_mcpwm esp_pm esp_timer esp_hw_support)
 return()
endif()
if(CORESE_LIVE)
 include("${CORESE_RUNTIME}/live.cmake")
 return()
endif()
set(crypto "$ENV{IDF_PATH}/components/bt/host/nimble/nimble/ext/tinycrypt")
# Read existing public codecs unchanged; do not enable a BLE owner in UI preparation.
idf_component_register(SRCS "ui.cpp" "mesh_diagnostics.cpp" "mesh_wire.cpp" "native_main.cpp"
 "../../t096/protocol/role_announcement.c" "../../t096/protocol/gnss_diagnostics.c" "../../t096/protocol/group_auth.c"
 "../../w12/main/g1_hmac.c" "${crypto}/src/sha256.c" "${crypto}/src/utils.c"
 INCLUDE_DIRS "." "${crypto}/include"
 REQUIRES M5Unified esp_psram esp_timer esp_hw_support)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
