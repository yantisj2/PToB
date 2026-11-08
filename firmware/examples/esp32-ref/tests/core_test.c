// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_clock.h"
#include "ref_counter.h"

#include <inttypes.h>
#include <stdio.h>

static unsigned checks = 0;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return false; \
    } \
} while (0)

static const uint32_t HZ = UINT32_C(80000000);
static const uint64_t SECOND = UINT64_C(80000000);

static bool conversion_cases(void)
{
    uint64_t ns = 123;
    CHECK(ref_ticks_to_ns(0, HZ, &ns) && ns == 0);
    CHECK(ref_ticks_to_ns(1, HZ, &ns) && ns == 12);
    CHECK(ref_ticks_to_ns(2, HZ, &ns) && ns == 25);
    CHECK(ref_ticks_to_ns(80, HZ, &ns) && ns == 1000);
    // A fractional third of a second is rounded down once.
    CHECK(ref_ticks_to_ns(1, 3, &ns) && ns == UINT64_C(333333333));
    CHECK(ref_ticks_to_ns(3, 3, &ns) && ns == UINT64_C(1000000000));
    // This conversion would overflow with a naive ticks * 1e9 expression.
    CHECK(ref_ticks_to_ns(UINT64_MAX, UINT32_MAX, &ns));
    CHECK(ns == UINT64_C(4294967297000000000));
    CHECK(ref_ticks_to_ns(UINT64_MAX, UINT32_C(1000000000), &ns));
    CHECK(ns == UINT64_MAX);
    ns = 123;
    CHECK(!ref_ticks_to_ns(UINT64_MAX, 1, &ns) && ns == 123);
    CHECK(!ref_ticks_to_ns(1, 0, &ns) && ns == 123);
    CHECK(!ref_ticks_to_ns(1, HZ, NULL));
    return true;
}

static bool counter_cases(void)
{
    RefCounter counter;
    uint64_t ticks = 999;
    CHECK(ref_counter_begin(&counter, UINT32_MAX - 79, 100, HZ));
    CHECK(ref_counter_poll(&counter, 0, 101, &ticks));
    CHECK(ticks == UINT64_C(4294967296));
    CHECK(ref_counter_poll(&counter, 80, 102, &ticks));
    CHECK(ticks == UINT64_C(4294967376));
    // Multiple real wraps are possible across a sequence of bounded polls.
    CHECK(ref_counter_begin(&counter, 0, 0, HZ));
    uint32_t raw = 0;
    for (uint64_t i = 1; i <= 20; ++i) {
        raw += UINT32_C(800000000);
        CHECK(ref_counter_poll(&counter, raw, i * UINT64_C(10000000), &ticks));
        CHECK(ticks == i * UINT64_C(800000000));
    }
    CHECK(ref_counter_begin(&counter, 0, 0, HZ));
    CHECK(ref_counter_poll(&counter, UINT32_C(2147483600), UINT64_C(26843545), &ticks));
    CHECK(ref_counter_begin(&counter, 0, 0, HZ));
    ticks = 999;
    CHECK(!ref_counter_poll(&counter, 80, UINT64_C(26843546), &ticks));
    CHECK(!counter.valid && ticks == 999);
    CHECK(!ref_counter_poll(&counter, 160, UINT64_C(26843547), &ticks));
    CHECK(ref_counter_begin(&counter, 0, 10, HZ));
    CHECK(!ref_counter_poll(&counter, 80, 9, &ticks));
    CHECK(ref_counter_begin(&counter, 100, 0, HZ));
    CHECK(!ref_counter_poll(&counter, 99, 1, &ticks));
    CHECK(ref_counter_begin(&counter, 0, 0, HZ));
    CHECK(!ref_counter_poll(&counter, UINT32_C(0x80000000), 1, &ticks));
    CHECK(ref_counter_begin(&counter, 0, 0, HZ));
    counter.extended_ticks = UINT64_MAX; // Exercise the finite 64-bit lifetime.
    CHECK(!ref_counter_poll(&counter, 1, 1, &ticks));
    CHECK(!ref_counter_begin(&counter, 0, 0, 0) && !counter.valid);
    CHECK(!ref_counter_begin(NULL, 0, 0, HZ));
    CHECK(!ref_counter_poll(NULL, 0, 0, &ticks));
    return true;
}

static bool phase_and_rate_cases(void)
{
    RefClock clock;
    RefClockReading reading;
    CHECK(ref_clock_begin(&clock, HZ, 0, 0, 20 * SECOND));
    CHECK(ref_clock_adjust(&clock, 0, 1000, 0, 0));
    CHECK(ref_clock_read(&clock, SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(1000001000));
    CHECK(ref_clock_adjust(&clock, 0, -1000, 0, 0));
    CHECK(ref_clock_read(&clock, SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(999999000));

    CHECK(ref_clock_begin(&clock, HZ, 0, 0, 20 * SECOND));
    CHECK(ref_clock_adjust(&clock, 0, 0, 2000, 1000));
    CHECK(ref_clock_read(&clock, SECOND / 2, &reading));
    CHECK(reading.time_ns == INT64_C(500000500) && reading.phase_remaining_ns == 1500);
    CHECK(ref_clock_read(&clock, 2 * SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(2000002000) && reading.phase_remaining_ns == 0);
    CHECK(ref_clock_read(&clock, 3 * SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(3000002000)); // Slew stops at the target.

    CHECK(ref_clock_begin(&clock, HZ, 0, 0, 20 * SECOND));
    CHECK(ref_clock_adjust(&clock, 0, 0, -2000, 1000));
    CHECK(ref_clock_read(&clock, SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(999999000) && reading.phase_remaining_ns == -1000);
    CHECK(ref_clock_read(&clock, 3 * SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(2999998000) && reading.phase_remaining_ns == 0);

    // Replacing a partly consumed correction must preserve the delivered value.
    const int64_t before_ns = reading.time_ns;
    CHECK(ref_clock_adjust(&clock, 3 * SECOND, 25000, 700, 1000));
    CHECK(ref_clock_read(&clock, 3 * SECOND, &reading));
    CHECK(reading.time_ns == before_ns && reading.phase_remaining_ns == 700);
    CHECK(clock.valid_until_tick == 20 * SECOND);
    return true;
}

static bool causal_and_lifetime_cases(void)
{
    RefClock clock;
    RefClockReading saved;
    RefClockReading reading;
    CHECK(ref_clock_begin(&clock, HZ, 0, 0, 4 * SECOND));
    CHECK(ref_clock_read(&clock, SECOND, &saved));
    const RefClock old_model = clock;
    CHECK(ref_clock_adjust(&clock, 2 * SECOND, 1000, 1000, 1000));
    CHECK(!ref_clock_read(&clock, SECOND, &reading));
    CHECK(ref_clock_read(&old_model, SECOND, &reading));
    CHECK(reading.time_ns == saved.time_ns && saved.time_ns == INT64_C(1000000000));
    CHECK(ref_clock_read(&clock, 4 * SECOND - 1, &reading));
    reading = (RefClockReading){123, 456};
    CHECK(!ref_clock_read(&clock, 4 * SECOND, &reading));
    CHECK(reading.time_ns == 123 && reading.phase_remaining_ns == 456);
    CHECK(!ref_clock_adjust(&clock, 4 * SECOND, 0, 0, 0));
    CHECK(clock.valid_until_tick == 4 * SECOND);
    ref_clock_invalidate(&clock);
    CHECK(!ref_clock_read(&clock, 3 * SECOND, &reading));
    // A new lifetime can explicitly restart at a different relative epoch.
    CHECK(ref_clock_begin(&clock, HZ, 50, 0, 100));
    CHECK(ref_clock_read(&clock, 50, &reading) && reading.time_ns == 0);
    CHECK(!ref_clock_read(&clock, 49, &reading));
    return true;
}

static bool refusal_cases(void)
{
    RefClock clock;
    RefClockReading reading;
    CHECK(ref_clock_begin(&clock, HZ, 0, 100, 4 * SECOND));
    CHECK(!ref_clock_adjust(&clock, SECOND, 50001, 0, 0));
    CHECK(!ref_clock_adjust(&clock, SECOND, -50001, 0, 0));
    CHECK(!ref_clock_adjust(&clock, SECOND, 0, 1, 50001));
    CHECK(!ref_clock_adjust(&clock, SECOND, 0, 1, 0));
    CHECK(!ref_clock_adjust(&clock, SECOND, 0, INT64_MIN, 1000));
    CHECK(clock.anchor_tick == 0 && clock.anchor_ns == 100 && clock.rate_ppb == 0);
    CHECK(ref_clock_read(&clock, SECOND, &reading));
    CHECK(reading.time_ns == INT64_C(1000000100));
    CHECK(!ref_clock_read(&clock, SECOND, NULL));
    CHECK(!ref_clock_adjust(NULL, 0, 0, 0, 0));
    CHECK(!ref_clock_begin(&clock, 0, 0, 0, SECOND) && !clock.valid);
    CHECK(!ref_clock_begin(&clock, HZ + 1, 0, 0, SECOND));
    CHECK(!ref_clock_begin(&clock, HZ, 0, -1, SECOND));
    CHECK(!ref_clock_begin(&clock, HZ, 10, 0, 10));
    CHECK(!ref_clock_begin(NULL, HZ, 0, 0, SECOND));
    CHECK(ref_clock_begin(&clock, HZ, 0, INT64_MAX, SECOND));
    CHECK(ref_clock_read(&clock, 0, &reading) && reading.time_ns == INT64_MAX);
    CHECK(!ref_clock_read(&clock, 1, &reading));
    CHECK(ref_clock_begin(&clock, 1, 0, 0, UINT64_MAX));
    CHECK(!ref_clock_read(&clock, UINT64_MAX - 1, &reading));
    CHECK(ref_clock_begin(&clock, HZ, 0, 0, SECOND));
    CHECK(ref_clock_adjust(&clock, 0, 0, INT64_MAX, 50000));
    CHECK(ref_clock_read(&clock, SECOND / 2, &reading));
    CHECK(reading.phase_remaining_ns == INT64_MAX - 25000);
    ref_clock_invalidate(NULL);
    return true;
}

static bool monotonicity_cases(void)
{
    const int32_t rates[] = {-50000, 0, 50000};
    const int64_t phases[] = {-500, 0, 500};
    for (unsigned r = 0; r < 3; ++r) {
        for (unsigned p = 0; p < 3; ++p) {
            RefClock clock;
            RefClockReading reading;
            CHECK(ref_clock_begin(&clock, HZ, 0, 0, 3 * SECOND));
            CHECK(ref_clock_adjust(&clock, 0, rates[r], phases[p], 50000));
            int64_t previous_ns = -1;
            // Cross rate-rounding boundaries and the end of either phase slew.
            for (uint64_t tick = 0; tick <= SECOND / 50; ++tick) {
                CHECK(ref_clock_read(&clock, tick, &reading));
                CHECK(reading.time_ns >= previous_ns);
                previous_ns = reading.time_ns;
            }
        }
    }
    return true;
}

int main(void)
{
    if (!conversion_cases() || !counter_cases() || !phase_and_rate_cases() ||
        !causal_and_lifetime_cases() || !refusal_cases() || !monotonicity_cases()) {
        return 1;
    }
    printf("PASS: conversion, rollover, phase/rate, causality, expiry, range and monotonicity (%u checks)\n",
           checks);
    printf("Host state sizes: counter=%zu bytes, clock=%zu bytes; no core heap allocation\n",
           sizeof(RefCounter), sizeof(RefClock));
    return 0;
}
