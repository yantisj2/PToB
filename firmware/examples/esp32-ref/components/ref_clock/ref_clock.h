// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_CLOCK_H
#define ESP32_REF_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

// 1 ppb contributes 1 ns of correction per nominal second.
// Bounds keep every allowed clock moving forward, even with negative slew.
#define REF_CLOCK_MAX_RATE_PPB INT32_C(50000)
#define REF_CLOCK_MAX_SLEW_PPB UINT32_C(50000)
#define REF_CLOCK_MAX_FREQUENCY_HZ UINT32_C(80000000)

// One software-clock model. Keep a copy when a prior model must remain readable.
// This struct describes arithmetic, not source identity or authentication.
// Access from one thread; a future hardware adapter must own synchronization.
typedef struct {
    uint64_t anchor_tick;
    uint64_t valid_until_tick;  // Exclusive deadline in the same counter lifetime.
    int64_t anchor_ns;         // Arbitrary relative epoch; zero is valid.
    int64_t phase_remaining_ns;
    int32_t rate_ppb;          // Positive makes software time advance faster.
    uint32_t slew_ppb;         // Maximum speed of consuming the phase backlog.
    uint32_t frequency_hz;
    bool valid;
} RefClock;

typedef struct {
    int64_t time_ns;
    int64_t phase_remaining_ns;
} RefClockReading;

// Convert a duration, rounding down to whole nanoseconds once.
// Splitting whole seconds from the remainder avoids ticks * 1,000,000,000
// overflowing. The output is unchanged on failure.
bool ref_ticks_to_ns(uint64_t ticks, uint32_t frequency_hz, uint64_t *ns);

// Explicitly begin a new model lifetime. This discards the previous model.
// No rate/phase correction is active initially. A failed begin invalidates it.
// This lesson supports 1..80 MHz; see the rounding/monotonicity explanation.
bool ref_clock_begin(RefClock *clock, uint32_t frequency_hz,
                     uint64_t anchor_tick, int64_t anchor_ns,
                     uint64_t valid_until_tick);

// Read only within [anchor_tick, valid_until_tick). No state changes.
// On refusal, *reading is unchanged; callers must check the bool result.
bool ref_clock_read(const RefClock *clock, uint64_t tick,
                    RefClockReading *reading);

// Preserve the old delivered value at now_tick, then replace rate and remaining
// phase. Positive phase means "catch up"; negative phase means "slow down".
// phase_ns is the new TOTAL backlog at now_tick, not an incremental addition.
// This operation neither estimates a rate nor extends the original deadline.
// Failure leaves the complete model unchanged. INT64_MIN phase is refused.
bool ref_clock_adjust(RefClock *clock, uint64_t now_tick, int32_t rate_ppb,
                      int64_t phase_ns, uint32_t slew_ppb);

// Withdraw the model after a counter/source/context change.
void ref_clock_invalidate(RefClock *clock);

#endif
