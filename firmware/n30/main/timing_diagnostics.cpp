// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "timing_diagnostics_api.hpp"
#include "timing_diagnostics.hpp"
#include "bounded_log.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <cinttypes>
namespace {
n30::timing::Recorder recorder;
std::atomic<bool> enabled{false};
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
// Sole main-task response owner, independent of host callback/servo stack.
n30::timing::Sample batch[4];
uint64_t now_us(){const auto n=esp_timer_get_time();return n>0?uint64_t(n):0;}
void capture(const v12::periodic_rx::Stamp& stamp,const adv_rx::BleAnchor& anchor,int8_t rssi){
 if(!enabled.load(std::memory_order_relaxed))return;
 portENTER_CRITICAL(&mux);const auto now=now_us();
 recorder.capture(stamp,anchor,rssi,now);enabled.store(recorder.snapshot(now).active,std::memory_order_relaxed);
 portEXIT_CRITICAL(&mux);
}
void pair(const v12::n30_probe::Pair& value){
 if(!enabled.load(std::memory_order_relaxed))return;
 portENTER_CRITICAL(&mux);const auto now=now_us();
 recorder.pair(value,now);enabled.store(recorder.snapshot(now).active,std::memory_order_relaxed);
 portEXIT_CRITICAL(&mux);
}
}
void n30_timing_init(){v12::n30_probe::capture_sink=capture;v12::n30_probe::pair_sink=pair;}
bool n30_timing_start(uint64_t id,uint64_t now,uint32_t duration,uint32_t every){
 portENTER_CRITICAL(&mux);const auto current=now_us();const bool ok=recorder.start(id,now,duration,every,current);
 enabled.store(recorder.snapshot(current).active,std::memory_order_relaxed);portEXIT_CRITICAL(&mux);return ok;
}
void n30_timing_stop(){portENTER_CRITICAL(&mux);recorder.stop();enabled.store(false,std::memory_order_relaxed);portEXIT_CRITICAL(&mux);}
void n30_timing_frame(v12::ConsoleFrame& f,uint32_t after,uint64_t){
 // Read monotonic time inside the owner lock. An earlier command-admission
 // timestamp must not look like a clock reversal after a host capture.
 portENTER_CRITICAL(&mux);const auto now=now_us();const auto state=recorder.snapshot(now);
 const auto count=recorder.read(after,batch,4,now);enabled.store(state.active,std::memory_order_relaxed);portEXIT_CRITICAL(&mux);
 f.format("N30_TIMING_STATUS schema=1 id=%016" PRIx64 " active=%u until_us=%" PRIu64 " every=%" PRIu32 " seen=%" PRIu32 " sampled=%" PRIu32 " matched=%" PRIu32 " overwritten=%" PRIu32 " pair_missing=%" PRIu32 " queued=%u oldest=%" PRIu32 " after=%" PRIu32 " count=%u\n",
  state.id,unsigned(state.active),state.until_us,state.every,state.seen,state.sampled,state.matched,state.overwritten,state.pair_missing,unsigned(state.queued),state.oldest,after,count);
 for(unsigned i=0;i<4;++i){
  if(i>=count){f.format("N30_TIMING_SAMPLE%u valid=0\n",i);continue;}
  const auto& r=batch[i];const auto& c=r.capture;const auto& a=r.anchor;const auto& p=r.pair;
  f.format("N30_TIMING_SAMPLE%u valid=1 n=%" PRIu32 " received_us=%" PRIu64 " local20=%" PRIu64 " available20=%" PRIu64 " epoch=%" PRIu32 " seq=%" PRIu32 " coarse=%" PRIu32 " fine=%u raw_metadata=%u associated=%u before20=%" PRIu64 " after20=%" PRIu64 " bt_slot=%" PRIu32 " bt_hus=%" PRIu32 " refine20=%d refined=%u phy=%u extra_us=%u enclosure_ns=%" PRIu32 " rssi=%d matched=%u root=%016" PRIx64 " boot=%016" PRIx64 " source=%" PRIu32 " train=%" PRIu32 " publication=%" PRIu32 " event=%u channel=%u remote_ns=%" PRIu64 " predicted_ns=%" PRId64 " causal=%u corrected20=%" PRIu64 " created_ns=%" PRIu64 " expires_ns=%" PRIu64 "\n",
   i,r.number,r.received_us,c.tick20,c.available20,c.epoch,c.sequence,c.coarse,unsigned(c.fine),unsigned(c.raw_metadata),unsigned(c.association_observed),a.before,a.after,a.time.half_slots,a.time.half_us,int(c.refinement_ticks),unsigned(c.refinement_applied),unsigned(c.phy),unsigned(c.radio_extra_us),c.map_enclosure_ns,int(r.rssi),unsigned(r.matched),p.root,p.boot,p.source,p.train,p.publication,unsigned(c.event),unsigned(c.channel),p.remote_ns,p.predicted_ns,unsigned(p.causal),p.available20,p.created_ns,p.expires_ns);
 }
 f.format("N30_TIMING_CONTRACT observer=WORKER_ONLY cursor=NONCONSUMING records=16 batch=4 max_ms=600000 timestamp=DESCRIPTOR_500NS_TO_TIMER_50NS RF_bias=UNKNOWN UTC_bound=UNKNOWN clock_update=NONE\n");
}
