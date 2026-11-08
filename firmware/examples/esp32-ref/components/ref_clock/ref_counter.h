// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_COUNTER_H
#define ESP32_REF_COUNTER_H

#include <stdbool.h>
#include <stdint.h>

// Extend successive samples of one free-running 32-bit hardware counter.
// The caller owns this state; access it from one thread or serialize access.
typedef struct {
    uint64_t extended_ticks;
    uint64_t last_poll_us;
    uint32_t last_raw;
    uint32_t frequency_hz;
    bool valid;
} RefCounter;

// Start a new counter lifetime. extended_ticks starts at raw_ticks, not at zero.
// poll_us is a separate monotonic watchdog clock, never a source of phase.
bool ref_counter_begin(RefCounter *counter, uint32_t raw_ticks,
                       uint64_t poll_us, uint32_t frequency_hz);

// Poll before half a raw wrap has elapsed, including during packet silence.
// Returns false on an ambiguous gap, backward watchdog time or overflow.
// Such a failure invalidates the counter until begin() is explicitly called.
// On failure, *extended_ticks is unchanged. A null argument is a caller error
// and does not change state. This function does not detect a stopped oscillator.
bool ref_counter_poll(RefCounter *counter, uint32_t raw_ticks,
                      uint64_t poll_us, uint64_t *extended_ticks);

#endif
