// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {struct ConsoleFrame;}
void n30_timing_init();
bool n30_timing_start(uint64_t id,uint64_t now_us,uint32_t duration_ms,uint32_t every);
void n30_timing_stop();
void n30_timing_frame(v12::ConsoleFrame&,uint32_t after,uint64_t now_us);
