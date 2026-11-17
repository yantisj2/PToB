// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_shadow.hpp"
#include "lora_shadow_input.h"
#include "bounded_log.hpp"
#include <cinttypes>
extern watch::LoRaSleepView watch_lora_sleep_snapshot();
extern watch::LoRaShadow::View watch_lora_shadow_snapshot();
void watch_lora_shadow_diagnostics(v12::ConsoleFrame& f){
 const auto s=watch_lora_shadow_snapshot();
 f.format("TWATCH_LORA_SHADOW version=2 shadow_only=0 active=%u tracking=%u reason=%u meta_phase=%u would_rx=%u pairs=%" PRIu32 " qualified=%" PRIu32 " covered=%" PRIu32 " outside=%" PRIu32 " misses=%" PRIu32 " resets=%" PRIu32 " meta_seen=%" PRIu32 " next_sync_us=%" PRIu64 " next_meta_us=%" PRIu64 " rx_us=%" PRIu64 " sleep_us=%" PRIu64 " unknown_us=%" PRIu64 " inactive_us=%" PRIu64 " lead_us=2000000 tail_us=8000000\n",unsigned(s.active),unsigned(s.tracking),unsigned(s.reason),unsigned(s.meta_phase),unsigned(s.would_rx),s.pairs,s.qualified,s.covered,s.outside,s.misses,s.resets,s.meta_seen,s.next_sync,s.next_meta,s.rx_us,s.sleep_us,s.unknown_us,s.inactive_us);
 const auto p=watch_lora_sleep_snapshot();
 f.format("TWATCH_LORA_SLEEP version=1 enabled=1 sleeping=%u active=%u sleeps=%" PRIu32 " wakes=%" PRIu32 " refused=%" PRIu32 " sleep_us=%" PRIu64 " rx_us=%" PRIu64 " transition_us=%" PRIu64 " wake_lead_us=0 poll_ms=50\n",unsigned(p.sleeping),unsigned(p.active),p.sleeps,p.wakes,p.refused,p.sleep_us,p.rx_us,p.last_transition);
}
