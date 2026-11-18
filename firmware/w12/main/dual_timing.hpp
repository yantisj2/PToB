// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "dual_clock.hpp"
#include "lora_view.h"
#if W12_GNSS_VERIFIED_INPUTS
#include "g1_gps_shadow.hpp"
#endif
namespace v12 {struct R2Status;struct ConsoleFrame;}
namespace gnss_pps {struct Observation;}
namespace w12 {
void dual_timing_update(const v12::R2Status&,const w12_lora_view&,uint64_t before,uint64_t now,bool ble_ready);
const ClockSegment& dual_timing_segment(uint64_t tick);
void dual_timing_diag(v12::ConsoleFrame&,uint64_t tick);
#if W12_GNSS_VERIFIED_INPUTS
void dual_timing_pps(const gnss_pps::Observation&,const v12::G1GpsShadow::Score&,uint32_t epoch,uint32_t second);
#endif
}
