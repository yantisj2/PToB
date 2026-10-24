// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Operator-selected relative-PPS adjustments, measured on one W12 fine receiver
// and a T096 child with its existing receiver correction. These are provisional
// transfers to other W12 units/profiles, not identified RF endpoint calibrations
// or UTC uncertainty bounds. Never fit these constants from live GNSS data.
struct W12PhaseOffsets {
  static constexpr int32_t ReceiveNs=7671;
  static constexpr int32_t RelayNs=4026;
  static G1ClockPrediction delivered(G1ClockPrediction raw){
    raw.receiver_phase_ns=raw.tick?ReceiveNs:0;
    return raw;
  }
};
}
