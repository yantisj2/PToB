// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "watch.hpp"
#include <algorithm>
namespace watch {
// All output is watch presentation. Radio admission and models remain owned
// independently by their original authenticated receivers.
struct Candidate { Model clock{};uint64_t root=0,boot=0;uint32_t epoch=0;bool fresh=false;
 uint64_t received_us=0;uint32_t pairs=0,sync_failures=0,setup_failures=0,losses=0; };
inline bool usable(const Candidate& c,uint64_t now){return c.clock.time_valid&&c.clock.utc_us&&now>=c.clock.sampled_us&&now-c.clock.sampled_us<250000&&now<c.clock.until_us;}
class MobileSelection {
 Source selected_=Source::None;Candidate previous_{};
 uint64_t last_=0,ble_since_=0,ble_lost_=0,lost_=0,scope_root_=0,scope_boot_=0;
 uint32_t scope_epoch_=0,switches_=0,gaps_=0;bool ble_seen_=false;
 uint64_t progress_at_=0,received_=0,failure_window_=0,failure_since_=0;
 uint32_t pairs_=0,sync_failures_=0,setup_failures_=0,losses_=0,failures_=0;
 bool counters_seen_=false;
 uint64_t ble_gap_us_=0;uint32_t ignored_failures_=0,return_resets_=0;uint8_t return_reset_reason_=0,return_block_=0,switch_reason_=0;
 void reset_return(uint8_t reason){
  if(ble_since_){if(return_resets_!=UINT32_MAX)++return_resets_;return_reset_reason_=reason;}
  ble_since_=0;ble_gap_us_=0;
 }
 public:
 static constexpr uint64_t GlitchUs=500000,ReturnUs=30000000,AbsentUs=30000000,FailureWindowUs=60000000,ReturnGapBudgetUs=1500000;
 Source source()const{return selected_;}uint32_t switches()const{return switches_;}uint32_t gaps()const{return gaps_;}
 uint64_t stable_us(uint64_t now)const{const auto end=ble_lost_?ble_lost_:now;return ble_since_&&end>=ble_since_?end-ble_since_:0;}
 uint64_t progress_age_us(uint64_t now)const{return progress_at_&&now>=progress_at_?now-progress_at_:UINT64_MAX;}
 uint64_t gap_us()const{return ble_gap_us_;}
 uint32_t ignored_failures()const{return ignored_failures_;}
 uint32_t return_resets()const{return return_resets_;}
 uint8_t return_reset_reason()const{return return_reset_reason_;}
 uint8_t return_block()const{return return_block_;}
 uint8_t switch_reason()const{return switch_reason_;}
 uint32_t model_expiries()const{return losses_;}
 uint32_t window_misses()const{return sync_failures_;}
 uint32_t setup_failures()const{return setup_failures_;}
 Model select(const Candidate& ble,const Candidate& lora,const Candidate& rc9,uint64_t now){
  if(last_&&now<last_)*this={};
  last_=now;
  const bool b=usable(ble,now),l=usable(lora,now),r=usable(rc9,now);
  const bool same=ble.root==scope_root_&&ble.boot==scope_boot_&&ble.epoch==scope_epoch_;
  // Presentation timestamps renew every poll; only original authenticated
  // receive evidence or corrected-pair progress can renew BLE availability.
  const bool progress=b&&ble.fresh&&
   ((!same)||ble.received_us!=received_||ble.pairs!=pairs_);
  if(progress){
   if(ble.pairs!=pairs_&&ble.pairs)progress_at_=now;
   else progress_at_=ble.received_us&&ble.received_us<=now?ble.received_us:0;
  }
  const auto delta=[](uint32_t next,uint32_t old){return next>=old?next-old:0u;};
  const uint32_t reported_failures=counters_seen_?std::max({delta(ble.sync_failures,sync_failures_),
   delta(ble.setup_failures,setup_failures_),delta(ble.losses,losses_)}):0;
  // A corrected permission may expire before a queued pair repairs it in the
  // same owner poll. Recent real RF progress, never a repaint/retained estimate,
  // distinguishes that repaired micro-gap from repeated failed acquisition.
  const bool repaired=b&&ble.fresh&&(same||progress)&&progress_age_us(now)<GlitchUs;
  const uint32_t failures=repaired?0:reported_failures;
  if(repaired)ignored_failures_+=std::min(reported_failures,UINT32_MAX-ignored_failures_);
  received_=ble.received_us;pairs_=ble.pairs;sync_failures_=ble.sync_failures;
  setup_failures_=ble.setup_failures;losses_=ble.losses;counters_seen_=true;
  if(!failure_window_||now-failure_window_>=FailureWindowUs){failure_window_=now;failures_=0;}
  if(failures){
   failures_=std::min<uint32_t>(3,failures_+std::min<uint32_t>(3,failures));
   if(failures_>=3&&!failure_since_)failure_since_=now;
   // A successful snapshot between failed trains does not immediately undo
   // the decision; recovery requires a new uninterrupted qualification window.
   reset_return(1);
  }
  const bool recent=progress_at_&&now>=progress_at_&&now-progress_at_<AbsentUs;
  const bool fresh_ble=b&&ble.fresh&&recent;
  if(fresh_ble){
   if(!ble_seen_||!same||!ble_since_){
    if(ble_seen_&&!same)reset_return(2);
    ble_since_=now;ble_gap_us_=0;
   }else if(ble_lost_){
    const auto gap=now-ble_lost_;
    if(gap>=GlitchUs||gap>ReturnGapBudgetUs-ble_gap_us_){reset_return(gap>=GlitchUs?3:4);ble_since_=now;}
    else {ble_since_+=gap;ble_gap_us_+=gap;} // Paused time never qualifies.
   }
   ble_lost_=0;ble_seen_=true;scope_root_=ble.root;scope_boot_=ble.boot;scope_epoch_=ble.epoch;
  }else if(ble_seen_&&!ble_lost_){ble_lost_=now;if(gaps_!=UINT32_MAX)++gaps_;}
  if(failure_since_&&fresh_ble&&!failures&&stable_us(now)>=ReturnUs){
   failure_since_=0;failures_=0;failure_window_=now;
  }
  return_block_=uint8_t(!b)|uint8_t(!ble.fresh)<<1|uint8_t(!recent)<<2|
   uint8_t(stable_us(now)<ReturnUs)<<3|uint8_t(failure_since_!=0)<<4;
  const bool stalled_ble=b&&ble.fresh&&(!progress_at_||now-progress_at_>=AbsentUs);
  const bool failed_ble=failure_since_&&now-failure_since_>=GlitchUs;
  const Candidate* current=selected_==Source::LOR?(l?&lora:nullptr):selected_==Source::RC9?(r?&rc9:nullptr):(b&&selected_!=Source::None?&ble:nullptr);
  const Candidate* chosen=current;
  if(!current){
   if(selected_!=Source::None&&!lost_)lost_=now;
   if(lost_&&now-lost_<GlitchUs&&previous_.clock.utc_us&&now>=previous_.clock.sampled_us){
    auto hold=previous_.clock;hold.utc_us+=now-hold.sampled_us;hold.sampled_us=now;hold.until_us=now+250000;
    hold.local_time=true;hold.locked=false;hold.rms_ns=UINT32_MAX;hold.hold_valid=false;return hold;
   }
   chosen=l&&lora.fresh?&lora:fresh_ble?&ble:r?&rc9:l?&lora:b?&ble:nullptr;
   // At first acquisition BLE need not meet sub-us lock to supply usable time.
   if(selected_==Source::None&&fresh_ble)chosen=&ble;
  }else{
   lost_=0;
   if(current==&ble&&l&&lora.fresh&&
      (failed_ble||stalled_ble||(!fresh_ble&&ble_lost_&&now-ble_lost_>=GlitchUs)))chosen=&lora;
   if(current==&lora&&fresh_ble&&stable_us(now)>=ReturnUs){
    chosen=&ble;failure_since_=0;failures_=0;failure_window_=now;
   }
   if(current==&rc9){if(l&&lora.fresh)chosen=&lora;else if(fresh_ble)chosen=&ble;}
  }
  if(!chosen){Model out;out.lora_on=lora.clock.lora_on;return out;}
  const auto next=chosen->clock.source;
  if(selected_!=next){
   switch_reason_=selected_==Source::None?1:chosen==&ble?2:chosen==&lora?(failed_ble?3:stalled_ble?4:current==&ble?5:6):7;
   if(switches_!=UINT32_MAX)++switches_;
   selected_=next;
  }
  // Retained coarse BLE is usable, but momentary loss must not flip its drum.
  if(chosen==&ble&&!fresh_ble&&ble_lost_&&now-ble_lost_<GlitchUs&&previous_.clock.utc_us){
   auto hold=previous_.clock;hold.utc_us+=now-hold.sampled_us;hold.sampled_us=now;hold.until_us=now+250000;
   hold.local_time=true;hold.locked=false;hold.rms_ns=UINT32_MAX;hold.hold_valid=false;return hold;
  }
  previous_=*chosen;lost_=0;
  auto output=chosen->clock;
  if(chosen==&ble&&!fresh_ble){output.local_time=true;output.holding=true;output.locked=false;output.rms_ns=UINT32_MAX;output.hold_valid=false;}
  return output;
 }
};
// Elapsed-time motion, independent of frame rate. Pointer values are separate
// from measured statistics; missing RMS moves to the maximum over30 seconds.
struct InstrumentMotion {
 SolarPosition solar_clock{};
 double rms=0,hold=0,reserve=0,phase=0,signal=0,tide=0;uint64_t last=0,loss_since=0,signal_loss_since=0;bool initialized=false;
 static double approach(double value,double target,double step){return target>value?std::min(target,value+step):std::max(target,value-step);}
 void apply(Model& m,uint64_t now){
  if(initialized&&now<last)*this={};
  const double dt=initialized?std::min(1.0,double(now-last)/1000000):0;last=now;initialized=true;
  const bool evidence=m.time_valid&&!m.local_time&&!m.holding;
  const bool known=evidence&&m.rms_ns!=UINT32_MAX;
  if(known)loss_since=0;else if(!loss_since)loss_since=now;
  const bool grace=!known&&loss_since&&now-loss_since<500000;
  const double target=known?std::min(1.0,m.rms_ns/10000.0):1;
  if(!grace)rms=approach(rms,target,dt/(known?3.0:30.0));
  const bool h=evidence&&sampled_hold(m);
  if(!grace)hold=approach(hold,h?(std::max(1.0,std::min(60.0,m.hold_ms/1000.0))-1)/59:0,dt/4);
  reserve=approach(reserve,m.battery>=0?std::min(100,int(m.battery))/100.0:0,dt/5);
  if(!grace)phase=approach(phase,evidence&&m.locked?1:m.time_valid&&(m.holding||m.local_time)?.5:0,dt/2);
  const bool signal_known=m.source_rssi>-127&&m.source_rssi<=20;
  if(signal_known)signal_loss_since=0;else if(!signal_loss_since)signal_loss_since=now;
  if(signal_known||now-signal_loss_since>=500000)signal=approach(signal,signal_known?std::clamp((m.source_rssi+110)/70.0,0.0,1.0):0,dt/3);
  if(m.calendar.tide_position_valid)tide=approach(tide,std::clamp(m.calendar.tide_position,0.0,1.0),dt/8);
  const auto& sky=m.calendar;
  const uint64_t utc=m.utc_us/1000000;
  if(m.time_valid&&sky.astronomy_valid&&sky.today.valid&&utc>=sky.sampled_utc&&utc-sky.sampled_utc<120){
   const double elapsed=(m.utc_us%1000000)/1000000.0+(now>=m.sampled_us?(now-m.sampled_us)/1000000.0:0);
   const auto target=solar_opposition(solar::civil_angle(utc,sky.local_offset_minutes,elapsed));
   const double dx=target.x-solar_clock.x,dy=target.y-solar_clock.y,d=std::hypot(dx,dy),step=12*dt;
   // Move inside the convex safe orbit at no more than 12px/s on acquisition.
   // Normal astronomical motion is far slower and reaches the exact trajectory.
   if(d>0){const double fraction=std::min(1.0,step/d);solar_clock.x+=dx*fraction;solar_clock.y+=dy*fraction;}
  }
  m.solar_clock=solar_clock;
  m.signal_position=signal;m.tide_position=tide;
  m.motion=true;m.rms_position=rms;m.hold_position=hold;m.reserve_position=reserve;m.phase_position=phase;
 }
};
}
