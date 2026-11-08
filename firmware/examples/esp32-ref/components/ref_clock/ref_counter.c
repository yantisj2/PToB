// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_counter.h"

#include <stddef.h>

#define HALF_WRAP_TICKS UINT32_C(0x80000000)
#define MICROSECONDS_PER_SECOND UINT64_C(1000000)

bool ref_counter_begin(RefCounter *counter, uint32_t raw_ticks,
                       uint64_t poll_us, uint32_t frequency_hz)
{
    if (counter == NULL) {
        return false;
    }
    *counter = (RefCounter){0};
    if (frequency_hz == 0) {
        return false;
    }
    counter->extended_ticks = raw_ticks;
    counter->last_raw = raw_ticks;
    counter->last_poll_us = poll_us;
    counter->frequency_hz = frequency_hz;
    counter->valid = true;
    return true;
}

bool ref_counter_poll(RefCounter *counter, uint32_t raw_ticks,
                      uint64_t poll_us, uint64_t *extended_ticks)
{
    if (counter == NULL || extended_ticks == NULL || !counter->valid) {
        return false;
    }

    // Modular subtraction alone cannot tell one wrap from several wraps.
    // A separate watchdog rejects silence long enough to make that ambiguous.
    // Subtract one before dividing so equality with half a wrap is rejected.
    const uint64_t max_gap_us =
        ((uint64_t)HALF_WRAP_TICKS * MICROSECONDS_PER_SECOND - 1) /
        counter->frequency_hz;
    if (poll_us < counter->last_poll_us ||
        poll_us - counter->last_poll_us > max_gap_us) {
        counter->valid = false;
        return false;
    }

    // Unsigned subtraction crosses a single rollover naturally.
    const uint32_t delta_ticks = raw_ticks - counter->last_raw;
    if (delta_ticks >= HALF_WRAP_TICKS ||
        counter->extended_ticks > UINT64_MAX - delta_ticks) {
        counter->valid = false;
        return false;
    }

    counter->extended_ticks += delta_ticks;
    counter->last_raw = raw_ticks;
    counter->last_poll_us = poll_us;
    *extended_ticks = counter->extended_ticks;
    return true;
}
