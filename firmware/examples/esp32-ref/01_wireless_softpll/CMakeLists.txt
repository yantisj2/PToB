# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.2.0")
if(NOT EXISTS "${REF_LOCAL_HEADER}")
    message(FATAL_ERROR "Use tools/build_01.py to supply local enrollment and board inputs")
endif()
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_ref_01)
