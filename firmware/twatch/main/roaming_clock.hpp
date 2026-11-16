// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_retained_estimate.hpp"
namespace watch {
// The watch keeps one reference-counter domain for its entire boot. BLE PA
// generations are owners of observations, not new oscillator domains. This
// value is only a starting rate for a successor's first authenticated physical
// pair; it carries no old phase, source permission, quality or trained flag.
inline double roaming_rate(const v12::G1RetainedEstimate* held,uint64_t original20,uint64_t available20){
 if(!held||!held->trained||!held->root||!held->boot||!held->source||!held->original20||
    !held->clock.tick||held->clock.tick<held->original20||original20<held->clock.tick||
    available20<original20||held->clock.output_correction_ns||held->clock.receiver_phase_ns||
    !std::isfinite(held->clock.rate)||std::abs(held->clock.rate)>50000||
    held->clock.at(original20)<=0||held->clock.at(available20)<=0)return 0;
 return held->clock.rate;
}
}
