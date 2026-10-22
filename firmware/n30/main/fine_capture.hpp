// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {struct ConsoleFrame;}
// Default OFF. No pins or peripheral are initialized before a bound START.
bool n30_fine_start(uint64_t id,uint64_t factory,uint64_t now_us,uint32_t duration_ms,uint32_t every);
// Fixed GPIO4/5 only; total edges4..24 in multiples4, spacing50..1000ms.
bool n30_fine_probe(uint64_t id,uint64_t factory,uint64_t now_us,uint32_t spacing_ms,uint32_t total_edges);
void n30_fine_stop();
void n30_fine_poll(uint64_t now_us);
void n30_fine_frame(v12::ConsoleFrame&,uint32_t after,uint64_t now_us);

// Maintenance must include every partial-start/failed-stop resource.
bool n30_fine_released();
bool n30_fine_cleanup_fault();
// Cached owner state only: never starts a timer, latches CURRENT or drives pins.
struct N30FineClockStatus {uint64_t until_us=0;uint32_t hz=0;bool active=false,healthy=false;};
N30FineClockStatus n30_fine_clock_status(uint64_t now_us);
