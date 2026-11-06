// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
struct gm_clock {uint64_t tick,ns,first_tick;double phase,rate,fast_accel,accel,p[4][4];uint32_t points;};
void gm_clear(struct gm_clock*);
bool gm_point(struct gm_clock*,uint64_t tick16,uint64_t corrected_event_ns);
bool gm_observed_point(struct gm_clock*,uint64_t tick16,uint64_t corrected_event_ns);
bool gm_ready(const struct gm_clock*);
// Observational display only; does not change gm_ready qualification.
bool gm_observed_predict(const struct gm_clock*,uint64_t tick16,uint64_t* ns);
bool gm_predict(const struct gm_clock*,uint64_t tick16,uint64_t* ns);
// Fixed120s decay, caller's original integer-counter guards bound0..300s.
double n30_lora_decay(double seconds);
double n30_lora_decay_fast(double seconds);
