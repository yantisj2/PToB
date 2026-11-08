// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_softpll.h"

#include <math.h>
#include <string.h>

// 16 Hz observations fill this readable, uncompressed history in four seconds.
// A model expires 500 ms after its ORIGINAL, not 500 ms after its follow-up.
static const uint64_t SECOND = REF_MS_TICKS(1000);
static const uint64_t PAIR_AGE = REF_MS_TICKS(250);
static const uint64_t MODEL_AGE = REF_MS_TICKS(500);

void ref_pll_begin(RefSoftPll *pll)
{
    memset(pll, 0, sizeof(*pll));
    pll->state = REF_PLL_ACQUIRE;
    pll->generation = 1;
}

RefPrediction ref_pll_predict(const RefSoftPll *pll, uint64_t event_tick)
{
    RefClockReading reading;
    if (ref_clock_read(&pll->clock, event_tick, &reading)) {
        return (RefPrediction){reading.time_ns, true};
    }
    return (RefPrediction){0, false};
}

// Center each RF channel separately when estimating slope. Changing the channel
// mix should not turn a fixed per-channel offset into an oscillator-rate change.
// All floating point is on short, relative coordinates, never an absolute epoch.
static bool fit(const RefSoftPll *pll, uint64_t now, int64_t *target, int32_t *rate)
{
    if (pll->count < 16) return false;
    const RefPllPoint *origin = &pll->points[pll->count == REF_PLL_POINTS ? pll->next : 0];
    if (pll->last_event_tick - origin->local_tick < 2 * SECOND) return false;

    struct { double x, y; unsigned n; } channels[REF_PLL_CHANNELS] = {{0}};
    double x[REF_PLL_POINTS], y[REF_PLL_POINTS];
    for (size_t i = 0; i < pll->count; ++i) {
        const RefPllPoint *p = &pll->points[i];
        x[i] = (double)(p->local_tick - origin->local_tick) / REF_PLL_HZ;
        y[i] = (double)(p->reference_ns - origin->reference_ns) - x[i] * 1e9;
        channels[p->channel].x += x[i];
        channels[p->channel].y += y[i];
        ++channels[p->channel].n;
    }
    unsigned degrees = 0;
    for (unsigned c = 0; c < REF_PLL_CHANNELS; ++c) {
        if (!channels[c].n) continue;
        degrees += channels[c].n - 1;
        channels[c].x /= channels[c].n;
        channels[c].y /= channels[c].n;
    }
    if (degrees < 8) return false;
    double xy = 0, xx = 0;
    for (size_t i = 0; i < pll->count; ++i) {
        const unsigned c = pll->points[i].channel;
        const double dx = x[i] - channels[c].x;
        xy += dx * (y[i] - channels[c].y);
        xx += dx * dx;
    }
    if (xx < 0.01) return false;
    const double slope = xy / xx; // ns/s, numerically equal to ppb.
    if (!isfinite(slope) || fabs(slope) > REF_CLOCK_MAX_RATE_PPB) return false;
    double intercept = 0;
    for (size_t i = 0; i < pll->count; ++i) intercept += y[i] - slope * x[i];
    intercept /= (double)pll->count;
    uint64_t elapsed_ns;
    if (!ref_ticks_to_ns(now - origin->local_tick, REF_PLL_HZ, &elapsed_ns) ||
        elapsed_ns > INT64_MAX) return false;
    const double correction = intercept + slope * ((double)elapsed_ns / 1e9);
    if (!isfinite(correction) || fabs(correction) > 1e9 ||
        origin->reference_ns > INT64_MAX - (int64_t)elapsed_ns - INT64_C(1000000000)) return false;
    *target = origin->reference_ns + (int64_t)elapsed_ns + (int64_t)llround(correction);
    *rate = (int32_t)llround(slope);
    return *target >= 0;
}

static void score(RefSoftPll *pll, int64_t reference_ns, RefPrediction prior)
{
    if (!prior.valid) { ++pll->unavailable; return; }
    const int64_t error = reference_ns - prior.ns;
    pll->last_error_ns = error;
    const uint64_t magnitude = (uint64_t)(error < 0 ? -error : error);
    if (magnitude > pll->maximum_error_ns) pll->maximum_error_ns = magnitude;
    ++pll->predicted;
    pll->errors[pll->error_next] = error;
    pll->error_next = (pll->error_next + 1) % REF_PLL_POINTS;
    if (pll->error_count < REF_PLL_POINTS) ++pll->error_count;
    double squares = 0;
    for (size_t i = 0; i < pll->error_count; ++i) {
        const double e = (double)pll->errors[i];
        squares += e * e;
    }
    pll->causal_rms_ns = sqrt(squares / (double)pll->error_count);
}

bool ref_pll_observe(RefSoftPll *pll, uint64_t event_tick, uint64_t available_tick,
                     int64_t reference_ns, uint8_t channel, RefPrediction prior)
{
    if (pll->state == REF_PLL_FAULT || reference_ns < 0 || channel >= REF_PLL_CHANNELS ||
        available_tick < event_tick || available_tick - event_tick > PAIR_AGE ||
        (pll->accepted && event_tick <= pll->last_event_tick) ||
        event_tick > UINT64_MAX - MODEL_AGE ||
        (prior.valid && prior.ns < 0)) { ++pll->refused; return false; }

    // Keep the error, including a bad one, before this pair enters the fit.
    score(pll, reference_ns, prior);
    if (prior.valid && (pll->last_error_ns > 1000000 || pll->last_error_ns < -1000000)) {
        ++pll->refused;
        ref_clock_invalidate(&pll->clock);
        pll->state = REF_PLL_FAULT;
        return false;
    }
    if (pll->accepted && event_tick - pll->last_event_tick >= MODEL_AGE) {
        pll->count = pll->next = pll->error_count = pll->error_next = 0;
        pll->trained = pll->lock_candidate = false;
    }
    pll->points[pll->next] = (RefPllPoint){event_tick, reference_ns, channel};
    pll->next = (pll->next + 1) % REF_PLL_POINTS;
    if (pll->count < REF_PLL_POINTS) ++pll->count;
    pll->last_event_tick = event_tick;
    ++pll->accepted;

    uint64_t delivery_ns;
    if (!ref_ticks_to_ns(available_tick - event_tick, REF_PLL_HZ, &delivery_ns) ||
        reference_ns > INT64_MAX - (int64_t)delivery_ns) return false;
    int64_t target = reference_ns + (int64_t)delivery_ns;
    int32_t rate = (int32_t)llround(pll->rate_ppb);
    const bool fitted = fit(pll, available_tick, &target, &rate);
    pll->trained = fitted;
    if (fitted) pll->rate_ppb = rate;

    RefClockReading before;
    int64_t phase = 0;
    if (!ref_clock_read(&pll->clock, available_tick, &before)) {
        // A new valid segment may seed an epoch. There was no available output
        // at this instant to preserve. Ordinary updates below never step time.
        if (!ref_clock_begin(&pll->clock, REF_PLL_HZ, available_tick, target,
                             event_tick + MODEL_AGE)) return false;
        pll->error_count = pll->error_next = 0;
        pll->lock_candidate = false;
        ++pll->generation;
    } else {
        phase = target - before.time_ns;
        RefClock next = pll->clock;
        // Only a NEW accepted physical pair may renew this deadline.
        next.valid_until_tick = event_tick + MODEL_AGE;
        const uint32_t slew = phase > -2000 && phase < 2000 ? 1000 : 50000;
        if (!ref_clock_adjust(&next, available_tick, rate, phase, slew)) return false;
        pll->clock = next;
    }
    const bool qualified = fitted && pll->error_count >= 32 &&
        pll->causal_rms_ns < 1000 && phase > -1000 && phase < 1000;
    if (!qualified) pll->lock_candidate = false;
    else if (!pll->lock_candidate) {
        pll->lock_candidate = true;
        pll->lock_since_tick = available_tick;
    }
    pll->state = !fitted ? REF_PLL_ACQUIRE :
        qualified && available_tick - pll->lock_since_tick >= 2 * SECOND ? REF_PLL_LOCK : REF_PLL_TRACK;
    return true;
}

bool ref_pll_read(RefSoftPll *pll, uint64_t now_tick, RefClockReading *reading)
{
    if (pll->state == REF_PLL_FAULT) return false;
    if (ref_clock_read(&pll->clock, now_tick, reading)) return true;
    if (pll->accepted) pll->state = REF_PLL_LOST;
    pll->lock_candidate = false;
    return false;
}

const char *ref_pll_state_name(RefPllState state)
{
    switch (state) {
        case REF_PLL_ACQUIRE: return "ACQUIRING";
        case REF_PLL_TRACK: return "TRACKING";
        case REF_PLL_LOCK: return "LOOP LOCK";
        case REF_PLL_LOST: return "SIGNAL LOST";
        default: return "TIMING FAULT";
    }
}
