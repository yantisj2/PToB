// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_clock.h"

#include <stddef.h>

#define NANOSECONDS_PER_SECOND UINT64_C(1000000000)

bool ref_ticks_to_ns(uint64_t ticks, uint32_t frequency_hz, uint64_t *ns)
{
    if (frequency_hz == 0 || ns == NULL) {
        return false;
    }
    const uint64_t seconds = ticks / frequency_hz;
    const uint64_t remainder_ticks = ticks % frequency_hz;
    if (seconds > UINT64_MAX / NANOSECONDS_PER_SECOND) {
        return false;
    }
    const uint64_t whole_ns = seconds * NANOSECONDS_PER_SECOND;
    // remainder_ticks < frequency_hz <= UINT32_MAX, so this product fits uint64.
    const uint64_t fraction_ns =
        remainder_ticks * NANOSECONDS_PER_SECOND / frequency_hz;
    if (fraction_ns > UINT64_MAX - whole_ns) {
        return false;
    }
    *ns = whole_ns + fraction_ns;
    return true;
}

bool ref_clock_begin(RefClock *clock, uint32_t frequency_hz,
                     uint64_t anchor_tick, int64_t anchor_ns,
                     uint64_t valid_until_tick)
{
    if (clock == NULL) {
        return false;
    }
    *clock = (RefClock){0};
    if (frequency_hz == 0 || frequency_hz > REF_CLOCK_MAX_FREQUENCY_HZ ||
        anchor_ns < 0 || valid_until_tick <= anchor_tick) {
        return false;
    }
    clock->frequency_hz = frequency_hz;
    clock->anchor_tick = anchor_tick;
    clock->anchor_ns = anchor_ns;
    clock->valid_until_tick = valid_until_tick;
    clock->valid = true;
    return true;
}

// At the allowed rate limits, both products below fit uint64 even when the
// elapsed interval is INT64_MAX ns. Split the interval to keep that proof simple.
static uint64_t correction_magnitude(uint64_t elapsed_ns, uint32_t ppb)
{
    return (elapsed_ns / NANOSECONDS_PER_SECOND) * ppb +
           (elapsed_ns % NANOSECONDS_PER_SECOND) * ppb /
               NANOSECONDS_PER_SECOND;
}

static bool add_signed(int64_t a, int64_t b, int64_t *sum)
{
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
        return false;
    }
    *sum = a + b;
    return true;
}

bool ref_clock_read(const RefClock *clock, uint64_t tick,
                    RefClockReading *reading)
{
    if (clock == NULL || reading == NULL || !clock->valid ||
        clock->frequency_hz == 0 || tick < clock->anchor_tick ||
        clock->frequency_hz > REF_CLOCK_MAX_FREQUENCY_HZ ||
        tick >= clock->valid_until_tick ||
        clock->rate_ppb < -REF_CLOCK_MAX_RATE_PPB ||
        clock->rate_ppb > REF_CLOCK_MAX_RATE_PPB ||
        clock->slew_ppb > REF_CLOCK_MAX_SLEW_PPB ||
        clock->phase_remaining_ns == INT64_MIN) {
        return false;
    }

    uint64_t elapsed_ns = 0;
    if (!ref_ticks_to_ns(tick - clock->anchor_tick, clock->frequency_hz,
                         &elapsed_ns) || elapsed_ns > INT64_MAX) {
        return false;
    }

    const uint32_t rate_magnitude = (uint32_t)(clock->rate_ppb < 0 ?
        -clock->rate_ppb : clock->rate_ppb);
    const int64_t rate_ns = (int64_t)correction_magnitude(elapsed_ns, rate_magnitude);
    const int64_t signed_rate_ns = clock->rate_ppb < 0 ? -rate_ns : rate_ns;

    const int64_t phase_ns = clock->phase_remaining_ns;
    const uint64_t backlog_ns = (uint64_t)(phase_ns < 0 ? -phase_ns : phase_ns);
    const uint64_t budget_ns = correction_magnitude(elapsed_ns, clock->slew_ppb);
    const int64_t used_ns = (int64_t)(budget_ns < backlog_ns ? budget_ns : backlog_ns);
    const int64_t signed_slew_ns = phase_ns < 0 ? -used_ns : used_ns;

    int64_t value_ns = 0;
    if (!add_signed(clock->anchor_ns, (int64_t)elapsed_ns, &value_ns) ||
        !add_signed(value_ns, signed_rate_ns, &value_ns) ||
        !add_signed(value_ns, signed_slew_ns, &value_ns) || value_ns < 0) {
        return false;
    }
    *reading = (RefClockReading){value_ns, phase_ns - signed_slew_ns};
    return true;
}

bool ref_clock_adjust(RefClock *clock, uint64_t now_tick, int32_t rate_ppb,
                      int64_t phase_ns, uint32_t slew_ppb)
{
    RefClockReading before;
    if (rate_ppb < -REF_CLOCK_MAX_RATE_PPB || rate_ppb > REF_CLOCK_MAX_RATE_PPB ||
        slew_ppb > REF_CLOCK_MAX_SLEW_PPB || phase_ns == INT64_MIN ||
        (phase_ns != 0 && slew_ppb == 0) ||
        !ref_clock_read(clock, now_tick, &before)) {
        return false;
    }

    // Reanchor at the old value before installing new parameters. At elapsed
    // zero both new corrections are zero: an update causes no time step.
    clock->anchor_tick = now_tick;
    clock->anchor_ns = before.time_ns;
    clock->rate_ppb = rate_ppb;
    clock->phase_remaining_ns = phase_ns;
    clock->slew_ppb = slew_ppb;
    return true;
}

void ref_clock_invalidate(RefClock *clock)
{
    if (clock != NULL) {
        clock->valid = false;
    }
}
