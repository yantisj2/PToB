// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// A BLE event/follow-up simulation: one free-running leader, two followers.
// This is executable teaching material, never a hardware precision result.
#include "ref_softpll.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
    RefSoftPll followers[2];
    const double ppm[] = {20, -15};
    struct { uint64_t tick; int64_t reference; uint8_t channel; RefPrediction prior; bool valid; } pending[2] = {{0}};
    for (unsigned i = 0; i < 2; ++i) ref_pll_begin(&followers[i]);
    uint64_t maximum_spread = 0;
    unsigned measured = 0, missing = 0;
    puts("ESP32-REF 01: deterministic wireless model, NO hardware measurement");
    printf("Local counter %d Hz. One 16 Hz train, delayed follow-ups, two oscillators and packet loss.\n", REF_CLOCK_HZ);
    puts("second   follower_A_state   follower_A_ppb   follower_B_state   follower_B_ppb");
    for (unsigned frame = 1; frame <= 960; ++frame) {
        const int64_t reference = (int64_t)frame * INT64_C(62500000);
        int64_t error[2] = {0}; bool both = true;
        for (unsigned i = 0; i < 2; ++i) {
            const uint64_t actual = (uint64_t)llround((double)frame * (REF_CLOCK_HZ / 16) * (1 + ppm[i] / 1e6));
            const uint8_t channel = (uint8_t)((frame * 13) % 37);
            // Quantize event capture to the descriptor's 500 ns grid. Channel
            // offsets are an injected test condition, not fitted calibration.
            const unsigned descriptor_step = REF_TICKS_PER_US / 2;
            const int64_t bias_ticks = ((int64_t)(channel % 3) - 1) * (REF_TICKS_PER_US / 10);
            const uint64_t rounded = (actual + descriptor_step / 2) / descriptor_step * descriptor_step;
            const uint64_t captured = (uint64_t)((int64_t)rounded + bias_ticks);
            const RefPrediction prediction = ref_pll_predict(&followers[i], captured);
            if (prediction.valid) error[i] = prediction.ns - reference;
            else both = false;
            const bool received = (frame + i * 11) % 53 != 0;
            if (!received) { ++missing; pending[i].valid = false; both = false; continue; }
            if (pending[i].valid && !ref_pll_observe(&followers[i], pending[i].tick, actual,
                    pending[i].reference, pending[i].channel, pending[i].prior)) {
                fprintf(stderr, "unexpected refusal at frame %u, follower %u\n", frame, i);
                return 1;
            }
            pending[i].tick = captured; pending[i].reference = reference;
            pending[i].channel = channel; pending[i].prior = prediction; pending[i].valid = true;
        }
        if (frame > 320 && both) {
            int64_t lo = 0, hi = 0; // Include the leader in the phase spread.
            for (unsigned i = 0; i < 2; ++i) { if (error[i] < lo) lo = error[i]; if (error[i] > hi) hi = error[i]; }
            const uint64_t spread = (uint64_t)(hi - lo);
            if (spread > maximum_spread) maximum_spread = spread;
            ++measured;
        }
        if (frame % 160 == 0) printf("%6u   %-16s   %+14.0f   %-16s   %+14.0f\n", frame / 16,
            ref_pll_state_name(followers[0].state), followers[0].rate_ppb,
            ref_pll_state_name(followers[1].state), followers[1].rate_ppb);
    }
    printf("Synthetic late-window spread: maximum=%" PRIu64 " ns, scored=%u/640 epochs; dropped RX=%u/1920\n",
           maximum_spread, measured, missing);
    if (measured < 500 || maximum_spread >= 1000 || followers[0].state != REF_PLL_LOCK || followers[1].state != REF_PLL_LOCK)
        return 1;
    RefClockReading ignored;
    if (ref_pll_read(&followers[0], REF_MS_TICKS(65000), &ignored) || followers[0].state != REF_PLL_LOST) return 1;
    puts("PASS: both loops converged in this simulation; a silent source expired. Physical accuracy remains UNKNOWN.");
    return 0;
}
