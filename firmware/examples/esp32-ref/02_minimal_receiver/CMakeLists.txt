# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0")
if(NOT EXISTS "${REF_LOCAL_HEADER}")
    message(FATAL_ERROR "Use tools/build_02.py for the standalone W12 or console demo")
endif()
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32_ref_02)
