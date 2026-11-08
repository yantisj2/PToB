// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lesson.h"
#include "ref_clock.h"
#include "ref_counter.h"

#include <inttypes.h>

// These are simulated inputs. No timer is sampled and no wall clock is read.
static const uint32_t COUNTER_HZ = UINT32_C(80000000);
static const uint64_t SECOND_TICKS = UINT64_C(80000000);

static bool counter_and_units(FILE *output)
{
    fputs("\n1. Counter rollover and units\n", output);
    RefCounter counter;
    uint64_t extended = 0;
    const uint32_t raw_before_wrap = UINT32_MAX - 79;
    if (!ref_counter_begin(&counter, raw_before_wrap, 0, COUNTER_HZ) ||
        !ref_counter_poll(&counter, 0, 1, &extended)) {
        return false;
    }

    const uint64_t elapsed_ticks = extended - raw_before_wrap;
    uint64_t elapsed_ns = 0;
    uint64_t one_tick_ns = 0;
    if (!ref_ticks_to_ns(elapsed_ticks, COUNTER_HZ, &elapsed_ns) ||
        !ref_ticks_to_ns(1, COUNTER_HZ, &one_tick_ns)) {
        return false;
    }
    fprintf(output, "raw: %" PRIu32 " -> 0; elapsed: %" PRIu64 " ticks = %" PRIu64 " ns\n",
            raw_before_wrap, elapsed_ticks, elapsed_ns);
    fprintf(output, "one 80 MHz tick is 12.5 ns; integer conversion returns %" PRIu64 " ns\n",
            one_tick_ns);
    fputs("Keep the ticks. Convert a whole interval once to avoid repeated rounding.\n", output);
    return true;
}

static bool frequency_error(FILE *output)
{
    fputs("\n2. A fast oscillator and a known rate correction\n", output);
    RefClock nominal;
    if (!ref_clock_begin(&nominal, COUNTER_HZ, 0, 0, 20 * SECOND_TICKS)) {
        return false;
    }
    RefClock corrected = nominal;
    // A +20 ppm oscillator accumulates 80,001,600 ticks per reference second.
    // Apply a manually supplied first-order inverse: -20,000 ppb.
    // An automatic frequency estimator belongs in the next lesson.
    if (!ref_clock_adjust(&corrected, 0, -20000, 0, 0)) {
        return false;
    }
    fputs("reference_s   nominal_error_ns   corrected_error_ns\n", output);
    for (uint64_t seconds = 1; seconds <= 10; seconds += 3) {
        const uint64_t local_tick = seconds * UINT64_C(80001600);
        const int64_t reference_ns = (int64_t)seconds * INT64_C(1000000000);
        RefClockReading raw_reading;
        RefClockReading corrected_reading;
        if (!ref_clock_read(&nominal, local_tick, &raw_reading) ||
            !ref_clock_read(&corrected, local_tick, &corrected_reading)) {
            return false;
        }
        fprintf(output, "%11" PRIu64 "   %+16" PRId64 "   %+18" PRId64 "\n",
                seconds, raw_reading.time_ns - reference_ns,
                corrected_reading.time_ns - reference_ns);
    }
    fputs("The small residual includes the first-order rate approximation and rounding.\n", output);
    return true;
}

static bool phase_slew(FILE *output)
{
    fputs("\n3. Correct phase gradually, preserving the current value\n", output);
    RefClock clock;
    RefClockReading before;
    RefClockReading after;
    if (!ref_clock_begin(&clock, COUNTER_HZ, 0, 0, 10 * SECOND_TICKS) ||
        !ref_clock_read(&clock, SECOND_TICKS, &before) ||
        !ref_clock_adjust(&clock, SECOND_TICKS, 0, 2000, 1000) ||
        !ref_clock_read(&clock, SECOND_TICKS, &after)) {
        return false;
    }
    fprintf(output, "at adjustment: before=%" PRId64 " ns, after=%" PRId64 " ns\n",
            before.time_ns, after.time_ns);
    fputs("elapsed_s   applied_phase_ns   remaining_phase_ns\n", output);
    for (uint64_t seconds = 0; seconds <= 3; ++seconds) {
        RefClockReading reading;
        if (!ref_clock_read(&clock, (1 + seconds) * SECOND_TICKS, &reading)) {
            return false;
        }
        const int64_t nominal_ns = (int64_t)(1 + seconds) * INT64_C(1000000000);
        fprintf(output, "%9" PRIu64 "   %+16" PRId64 "   %+18" PRId64 "\n",
                seconds, reading.time_ns - nominal_ns, reading.phase_remaining_ns);
    }
    fputs("Positive phase catches up; negative phase slows the clock while time advances.\n", output);
    return true;
}

static bool delayed_observation(FILE *output)
{
    fputs("\n4. Save the prediction before a delayed correction arrives\n", output);
    RefClock clock;
    RefClockReading captured_prediction;
    const uint64_t event_tick = 2 * SECOND_TICKS;
    const uint64_t available_tick = event_tick + SECOND_TICKS / 4;
    if (!ref_clock_begin(&clock, COUNTER_HZ, 0, 0, 10 * SECOND_TICKS) ||
        !ref_clock_read(&clock, event_tick, &captured_prediction)) {
        return false;
    }

    // At capture, only this prediction is known. The reference value for that
    // event becomes available later, for example in a BLE follow-up packet.
    const int64_t reference_event_ns = INT64_C(2000001000);
    const int64_t saved_error_ns = reference_event_ns - captured_prediction.time_ns;
    const RefClock prior_model = clock;
    fprintf(output, "event at 2.000 s; correction available at 2.250 s\n");
    fprintf(output, "saved prediction=%" PRId64 " ns; pre-update error=%+" PRId64 " ns\n",
            captured_prediction.time_ns, saved_error_ns);

    // In this deliberately constant-rate scenario the phase error is unchanged
    // during delivery delay. Real varying-rate observations need projection
    // to the update tick; passing an old phase error blindly is insufficient.
    if (!ref_clock_adjust(&clock, available_tick, 0, saved_error_ns, 1000)) {
        return false;
    }
    RefClockReading historical;
    if (ref_clock_read(&clock, event_tick, &historical) ||
        !ref_clock_read(&prior_model, event_tick, &historical)) {
        return false;
    }
    fprintf(output, "new model refuses the old event; saved prior model still reads %" PRId64 " ns\n",
            historical.time_ns);
    fprintf(output, "the recorded error stays %+" PRId64 " ns after adjustment\n", saved_error_ns);
    return true;
}

static bool availability_limits(FILE *output)
{
    fputs("\n5. Refuse unavailable time\n", output);
    RefClock clock;
    RefClockReading reading;
    if (!ref_clock_begin(&clock, COUNTER_HZ, 0, 0, SECOND_TICKS) ||
        !ref_clock_adjust(&clock, SECOND_TICKS / 2, 0, 0, 0) ||
        ref_clock_read(&clock, SECOND_TICKS, &reading)) {
        return false;
    }
    fputs("read at the 1 s deadline: refused; an adjustment did not renew it\n", output);

    RefCounter counter;
    uint64_t ticks = 0;
    if (!ref_counter_begin(&counter, 0, 0, COUNTER_HZ) ||
        ref_counter_poll(&counter, 80, UINT64_C(26843546), &ticks) ||
        ref_counter_poll(&counter, 160, UINT64_C(26843547), &ticks)) {
        return false;
    }
    fputs("counter poll after half-wrap silence: refused; explicit restart required\n", output);
    fputs("A real adapter must also withdraw its clock when its counter lifetime changes.\n", output);
    return true;
}

bool ref_run_clock_lesson(FILE *output)
{
    if (output == NULL) {
        return false;
    }
    fputs("ESP32-REF 00: clock fundamentals\n"
          "Deterministic simulation. No hardware, BLE or UTC measurement.\n", output);
    const bool ok = counter_and_units(output) && frequency_error(output) &&
                    phase_slew(output) && delayed_observation(output) &&
                    availability_limits(output);
    fputs(ok ? "\nAll five exercises completed.\n" : "\nExercise failed.\n", output);
    return ok && !ferror(output);
}
