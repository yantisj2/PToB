// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdio>
// All existing command formats use strings, characters and integers only.
// The pinned Newlib integer scanner is already used by the W12 console.
#ifdef ESP_PLATFORM
#define N30_COMMAND_SCAN ::siscanf
#else
#define N30_COMMAND_SCAN std::sscanf
#endif
