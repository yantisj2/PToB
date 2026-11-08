// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_SOFTPLL_H
#define ESP32_REF_SOFTPLL_H

#include "ref_clock.h"
#include "ref_timebase.h"
#include <stddef.h>

#define REF_PLL_POINTS 64
#define REF_PLL_CHANNELS 37
#define REF_PLL_HZ REF_CLOCK_HZ

typedef enum { REF_PLL_ACQUIRE, REF_PLL_TRACK, REF_PLL_LOCK, REF_PLL_LOST,
               REF_PLL_FAULT } RefPllState;

typedef struct {
    uint64_t local_tick;
    int64_t reference_ns;
    uint8_t channel;
} RefPllPoint;

// Capture this prediction BEFORE processing the event's later follow-up.
typedef struct { int64_t ns; bool valid; } RefPrediction;

typedef struct {
    RefClock clock;
    RefPllPoint points[REF_PLL_POINTS];
    int64_t errors[REF_PLL_POINTS];
    size_t count, next, error_count, error_next;
    uint64_t last_event_tick, lock_since_tick, maximum_error_ns;
    uint32_t accepted, refused, predicted, unavailable, generation;
    int64_t last_error_ns;
    double causal_rms_ns, rate_ppb;
    RefPllState state;
    bool trained, lock_candidate;
} RefSoftPll;

void ref_pll_begin(RefSoftPll *pll);
RefPrediction ref_pll_predict(const RefSoftPll *pll, uint64_t event_tick);

// Inputs are already authenticated, joined to one event and in one session.
// Return false on malformed, stale or discontinuous input. A FAULT requires
// the transport owner to withdraw admission before explicitly beginning again.
bool ref_pll_observe(RefSoftPll *pll, uint64_t event_tick, uint64_t available_tick,
                     int64_t reference_ns, uint8_t channel, RefPrediction prior);
bool ref_pll_read(RefSoftPll *pll, uint64_t now_tick, RefClockReading *reading);
const char *ref_pll_state_name(RefPllState state);

#endif
