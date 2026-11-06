// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "lora_view.h"
#include "source_absence_policy.hpp"
namespace n30 {
constexpr uint64_t NoTimeRestartUs=ptob::NoTimeRestartUs;
// Preserve learned boot information through ordinary RF absence. This disables
// only the destructive absence timer; it supplies no fresh synchronization and
// leaves the independent driver/counter/cleanup watchdogs unchanged.
class NoTimeRetention {
 ptob::SourceAbsencePolicy policy_;
 public:
 bool expected(bool requested,bool ble_learned,const n30_lora_view& l,uint64_t now=0){
  // META cadence is300s: waiting for it (including one missed advertisement)
  // must not race the destructive300s absence timer. Actual owner faults are
  // still supervised separately. A stale worker snapshot is not healthy RX.
  const bool acquiring=l.requested&&l.active&&!l.fault&&now>=l.sampled_us&&now-l.sampled_us<250000;
  return policy_.observe(requested,ble_learned||l.metadata_cached||l.watch_valid,acquiring);
 }
};
}
