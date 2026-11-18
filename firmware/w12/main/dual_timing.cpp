// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "dual_timing.hpp"
#include "native_r2.hpp"
#include "bounded_log.hpp"
#include "fine_counter.hpp"
#if W12_GNSS_VERIFIED_INPUTS
#include "gnss_pps.hpp"
#endif
#include "w12_fine_clock.hpp"
#include "w12_lora_config.h"
#include <cinttypes>
#include <cstdio>

namespace w12 {
namespace {
DualClock clock;
struct LoRaModel {
 uint64_t tick=0,ns=0,origin=0,id=0,epoch=0,emitter=0,root=0,lo=0,active=0,until=0;
 int32_t rate=0,phase=0,correction=0;
 uint64_t at(uint64_t common)const{
  if(!id||common<fine::Counter::Base20||common>=until)return 0;
  const auto raw=(common-fine::Counter::Base20)*4;
  if(raw<origin)return 0;
  // Preserve the worker's staged floor to 20 then 16 MHz. The main tick
  // is rounded at 50 ns, so translation has up to 100 ns grid enclosure.
  const auto radio=(fine::Counter::Base20+(raw-origin)/4)*4/5;
  uint64_t projection=0;
  if(radio<id||!w12_lora_project(radio,tick,ns,rate,phase,&projection)||
     projection>uint64_t(INT64_MAX)-INT32_MAX||projection<=uint64_t(INT32_MAX))return 0;
  return uint64_t(int64_t(projection)+correction);
 }
};
#if W12_GNSS_VERIFIED_INPUTS
LoRaModel previous;
#endif
LoRaModel latest;
static_assert(sizeof(DualClock)+2*sizeof(LoRaModel)<=352);

uint64_t deadline(uint64_t until,uint64_t now,uint64_t tick){
 return until>now&&until-now<(UINT64_MAX-tick)/20?tick+(until-now)*20:0;
}
}

void dual_timing_update(const v12::R2Status& b,const w12_lora_view& l,uint64_t before,uint64_t now,bool ble_ready){
 uint64_t tick=0;
 if(!before||!w12_fine_current(&tick)||tick<before)return;
 // Translate the lease conservatively using the earlier latch. The extra
 // host/main snapshot allowance never renews the original radio deadlines.
 const auto lu=deadline(std::min(l.until_us,l.sampled_us+250000),now,before);
 const bool live=l.requested&&l.active&&!l.fault&&l.state==2&&l.metadata_known&&l.time_valid&&
  l.model_valid&&l.model_ns&&l.model_tick16&&l.edge_tick&&l.capture_epoch&&l.emitter&&l.root&&
  l.source_epoch&&now>=l.sampled_us&&lu>tick;
 if(bool(latest.id)!=live||(live&&(l.edge_tick!=latest.id||l.capture_epoch!=latest.epoch||
     l.model_tick16!=latest.tick||before>=latest.until))){
#if W12_GNSS_VERIFIED_INPUTS
  previous=latest;
#endif
  latest={};latest.lo=before;latest.active=tick;
  if(live){
   latest.tick=l.model_tick16;latest.ns=l.model_ns;latest.origin=l.origin80;latest.id=l.edge_tick;
   latest.epoch=l.capture_epoch;latest.emitter=l.emitter;latest.root=l.root;latest.rate=l.rate_ppb;latest.phase=l.phase_ns;
   if(l.root==PTOB_W12_LORA_CAL_ROOT&&l.emitter==PTOB_W12_LORA_CAL_EMITTER)latest.correction=PTOB_W12_LORA_CAL_NS;
  }
 }
 latest.until=live?lu:0;
 ClockInput ble;
 if(ble_ready&&b.group_member&&b.root_identity==b.g1_clock_origin){
  const auto utc=b.g1_clock_model.at(tick);
  if(utc>0)ble={uint64_t(utc),deadline(std::min({b.g1_corrected_until_us,b.g1_anchor_deadline_us,b.service_until_us,b.follower_snapshot_us+250000}),now,before),
   b.g1_corrected_original20,(uint64_t(b.g1_relative_quality.scope_generation)<<32)|b.publication,int32_t(std::lround(b.g1_clock_model.rate))};
 }
 clock.update(tick,ble,{latest.at(tick),latest.until,latest.id,latest.epoch,latest.rate});
}

const ClockSegment& dual_timing_segment(uint64_t tick){return clock.segment(tick);}

void dual_timing_diag(v12::ConsoleFrame& frame,uint64_t tick){
 const auto& c=clock.segment(tick);
 const auto ns=c.at(tick);
 frame.format(" common_mode=%u common_tick=%" PRIu64 " common_ns=%" PRIu64 " common_ppb=%" PRId32 " lora_origin80=%" PRIu64 " lora_bias_ns=%" PRId32,
  ns?unsigned(c.mode_at(tick)):0,tick,ns,c.rate,latest.origin,latest.correction);
}

#if W12_GNSS_VERIFIED_INPUTS
void dual_timing_pps(const gnss_pps::Observation& edge,const v12::G1GpsShadow::Score& ble,uint32_t epoch,uint32_t second){
 if(!v12::log_status().capture_active)return;
 const auto tick=edge.tick;
 const auto& l=tick>latest.active?latest:previous;
 const auto guard=(uint64_t(edge.map_bracket_ns)+149)/50;
 // Refuse either side of a model's activation bracket and the original
 // deadline when the PPS mapping enclosure overlaps a boundary.
 const bool race=(tick<=latest.active||tick-latest.active<=guard)&&
  (tick>=latest.lo||latest.lo-tick<=guard)&&latest.active;
 const bool mapped=edge.mapped&&edge.map_bracket_ns!=UINT32_MAX;
 const auto ns=mapped&&!race&&tick>l.active&&tick-l.active>guard&&l.until>tick&&l.until-tick>guard?l.at(tick):0;
 const auto& c=clock.segment(tick);
 std::printf("W12_DUAL_PPS n=%" PRIu32 " tick=%" PRIu64 " raw=%" PRIu64 " map=%u ref=%u epoch=%" PRIu32 " rmc=%" PRIu32
  " ble=%" PRIu64 " why=%u br=%016" PRIx64 " bs=%" PRIu32 " lora=%" PRIu64 " pair=%" PRIu64 " cap=%" PRIu64
  " root=%016" PRIx64 " emitter=%016" PRIx64 " bias=%" PRId32 " utc=%" PRIu64 " mode=%u enc=%" PRIu32 " race=%u\n",
  edge.number,tick,edge.raw_extended,unsigned(mapped),unsigned(ble.row.reference),epoch,second,
  ble.map_valid&&ble.row.permission?uint64_t(ble.utc_ns):0,unsigned(ble.row.reason),ble.map.root,ble.row.session,
  ns,l.id,l.epoch,l.root,l.emitter,l.correction,mapped?c.at(tick):0,unsigned(c.mode_at(tick)),edge.map_bracket_ns,unsigned(race));
}
#endif
}
