// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_TIMEBASE_H
#define ESP32_REF_TIMEBASE_H

#include <stdint.h>

// One local coordinate per firmware image. Wire timestamps are nanoseconds.
#ifndef REF_CLOCK_HZ
#define REF_CLOCK_HZ 20000000
#endif
#if REF_CLOCK_HZ != 20000000 && REF_CLOCK_HZ != 80000000
#error "Example 01 supports 20 MHz GPTimer or 80 MHz MCPWM capture"
#endif

#define REF_TICKS_PER_US (REF_CLOCK_HZ / 1000000)
#define REF_MS_TICKS(ms) (UINT64_C(1) * (REF_CLOCK_HZ / 1000) * (ms))

#endif
