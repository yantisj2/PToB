// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include "lora_view.h"
extern "C" {
#include "lora_thermal.h"
}
namespace watch::dual {
// Original XTAL20 bracket of a worker-owned MCPWM0 CURRENT latch. No caller
// outside the LoRa owner touches its capture peripheral or counter extension.
struct LoRaBridge {uint64_t before=0,after=0,tick16=0,available_us=0,epoch=0;};
struct Bridge {
 uint64_t before20=0,after20=0,xtal20=0,available20=0;
 uint64_t xtal_lower20=0,xtal_upper20=0;
 LoRaBridge radio{};
 bool convert(uint64_t edge,uint64_t& tick16,uint32_t& enclosure)const {
  tick16=0;enclosure=UINT32_MAX;
  const bool reverse=xtal_lower20&&xtal_upper20>=xtal_lower20;
  if(!before20||after20<before20||after20-before20>400||available20<after20||
     (reverse&&xtal_upper20-xtal_lower20>400)||
     edge<=available20||edge-available20>40000000||(!reverse&&!xtal20)||!radio.before||
     radio.after<radio.before||radio.after-radio.before>400||!radio.tick16||!radio.epoch)return false;
  // APB80 and XTAL20 are locked derivatives of the same physical crystal.
  // Carry the entire enclosing interval; midpoint is a coordinate, not an
  // RF/UTC bound. Anchors and their completed publication must precede PPS.
  const auto delta=edge-after20;
  const auto low_anchor=reverse?xtal_lower20:xtal20;
  const auto high_anchor=reverse?xtal_upper20:xtal20;
  if(low_anchor>UINT64_MAX-delta)return false;
  const auto lo=low_anchor+delta;
  if(high_anchor>UINT64_MAX-delta-(after20-before20))return false;
  const auto hi=high_anchor+delta+(after20-before20);
  if(lo<=radio.after||hi-radio.before>40000000)return false;
  const auto dl=lo-radio.after,dh=hi-radio.before;
  const auto lower=dl*4/5,upper=(dh*4+4)/5;
  if(radio.tick16>UINT64_MAX-upper)return false;
  tick16=radio.tick16+lower+(upper-lower)/2;
  const auto width=upper-lower;
  enclosure=uint32_t((width*125+3)/4+163); // half width + quantization
  return true;
 }
};
// Exact frozen delivered-model arithmetic, including both damped acceleration
// terms and the receiver's bounded phase slew. No rounded display UTC input.
inline bool lora_project(const n30_lora_view& v,uint64_t tick,uint64_t& ns,double* rate_ppb=nullptr){
 ns=0;
 if(!v.model_valid||v.model_kind!=1||!v.model_epoch||v.model_epoch!=v.capture_epoch||
    !v.model_tick16||tick<v.model_tick16||tick<v.edge_tick||
    tick-v.edge_tick>UINT64_C(120000000)*16||tick-v.model_tick16>UINT64_C(120000000)*16||
    std::abs(double(v.model_rate_milli_ppb))>100000000||
    std::abs(double(v.model_slow_accel_micro_ppb_s))+std::abs(double(v.model_fast_accel_micro_ppb_s))>1000000000)return false;
 const auto ticks=tick-v.model_tick16,nominal=ticks*125/2;
 const double dt=double(ticks)/16000000;
 const double g=120*(1-n30_lora_decay(dt)),gf=30*(1-n30_lora_decay_fast(dt));
 const double h=120*(dt-g),hf=30*(dt-gf);
 const double correction=double(v.model_rate_milli_ppb)*.001*dt+
  double(v.model_slow_accel_micro_ppb_s)*.000001*h+
  double(v.model_fast_accel_micro_ppb_s)*.000001*hf+double(v.model_phase_ns)*std::min(dt,1.0);
 if(!std::isfinite(correction)||std::abs(correction)>1e9)return false;
 const auto change=int64_t(std::llround(correction));uint64_t elapsed=nominal;
 if(change<0){if(nominal<uint64_t(-change))return false;elapsed-=uint64_t(-change);}
 else {if(elapsed>UINT64_MAX-uint64_t(change))return false;elapsed+=uint64_t(change);}
 if(!v.model_ns||v.model_ns>uint64_t(INT64_MAX)-elapsed)return false;
 ns=v.model_ns+elapsed;
 // Derivative of the very same frozen predictor. The one-second phase slew
 // is a phase actuator, not an oscillator-frequency observation.
 if(rate_ppb)*rate_ppb=double(v.model_rate_milli_ppb)*.001+
  double(v.model_slow_accel_micro_ppb_s)*.000001*g+
  double(v.model_fast_accel_micro_ppb_s)*.000001*gf;
 return true;
}
inline bool strict_lora(const n30_lora_view& v,uint64_t now){
 return v.requested&&v.active&&!v.fault&&v.state==2&&v.time_valid&&v.metadata_known&&v.model_valid&&
  v.request_generation&&v.capture_epoch&&v.model_epoch==v.capture_epoch&&v.sampled_us&&
  now>=v.sampled_us&&now-v.sampled_us<250000&&now<v.until_us&&v.emitter&&v.emitter_boot&&v.root&&v.root_boot;
}
enum class Mode:uint8_t {Unsynced,Ble,LoRa,Hold};
inline const char* mode_name(Mode m){switch(m){case Mode::Ble:return "BLE_FINE";case Mode::LoRa:return "LORA_COARSE";case Mode::Hold:return "HOLD";default:return "UNSYNCED";}}
struct Observation {
 uint64_t tick=0,utc_ns=0,until=0,scope=0,id=0,domain=0;
 uint32_t enclosure_ns=UINT32_MAX;double rate_ppb=0;
 bool eligible=false,calibrated=false;
 bool live(uint64_t now)const{return eligible&&scope&&id&&domain&&tick&&tick<=now&&now<until&&utc_ns&&
  utc_ns<=uint64_t(INT64_MAX)&&enclosure_ns!=UINT32_MAX&&std::isfinite(rate_ppb)&&std::abs(rate_ppb)<=50000;}
};
struct ClockView {
 uint64_t tick=0,utc_ns=0,instance=0,domain=0,holdover_age_us=0,updates=0,rejected=0,sampled_us=0;
 double rate_ppb=0;uint32_t uncertainty_floor_ns=UINT32_MAX;
 Mode mode=Mode::Unsynced;bool valid=false,authoritative=false,fine=false,enabled=false,calibrated=false;
};
// One counter, actuator and learned oscillator rate for the entire boot.
// Inputs have already passed independent receiver authentication/expiry gates.
// GNSS is deliberately absent from this interface. Activation is build-bound.
class Clock {
 uint64_t instance_=0,domain_=0,tick_=0,ns_=0,last_live_=0,updates_=0,rejected_=0,permission_=0;
 uint64_t ids_[2]{},scopes_[2]{},domains_[2]{},ble_since_=0,ble_last_=0,qual_scope_=0,qual_domain_=0,qual_id_=0;
 uint64_t coarse_last_=0;uint8_t selected_=2;
 double rate_=0,phase_=0,fraction_=0;Mode mode_=Mode::Unsynced;bool enabled_=false,fault_=false,calibrated_=false;
 static double slew(Mode m){return m==Mode::LoRa?5000:50000;}
 bool segment(uint64_t tick){
  if(!tick_||tick<tick_||tick-tick_>UINT64_C(86400)*20000000)return false;
  const double dt=double(tick-tick_)/20000000;
  const double applied=std::clamp(phase_,-slew(mode_)*dt,slew(mode_)*dt);
  const auto nominal=(tick-tick_)*50;
  const double correction=rate_*dt+applied+fraction_;
  const auto adjust=int64_t(std::llround(correction));
  fraction_=correction-double(adjust);
  if(adjust<0&&nominal<uint64_t(-adjust))return false;
  const auto elapsed=adjust<0?nominal-uint64_t(-adjust):nominal+uint64_t(adjust);
  if(ns_>uint64_t(INT64_MAX)-elapsed)return false;
  ns_+=elapsed;phase_-=applied;tick_=tick;return true;
 }
 bool advance(uint64_t tick){
  if((mode_==Mode::Ble||mode_==Mode::LoRa)&&permission_&&tick>=permission_){
   if(tick_ < permission_&&!segment(permission_))return false;
   mode_=Mode::Hold;phase_=0;last_live_=permission_;
  }
  return segment(tick);
 }
 public:
 Clock(uint64_t instance=0,uint64_t domain=0,bool enabled=false):instance_(instance),domain_(domain),enabled_(enabled){}
 ClockView view(uint64_t tick)const {
  auto copy=*this;ClockView v;v.instance=instance_;v.domain=domain_;v.tick=tick;v.enabled=enabled_;
  v.updates=updates_;v.rejected=rejected_;v.mode=mode_;v.rate_ppb=rate_;
  if(!fault_&&tick_&&copy.advance(tick)){v.valid=true;v.utc_ns=copy.ns_;v.mode=copy.mode_;v.authoritative=copy.mode_==Mode::Ble||copy.mode_==Mode::LoRa;
   v.fine=copy.mode_==Mode::Ble;v.calibrated=v.authoritative&&copy.calibrated_;
   v.uncertainty_floor_ns=!v.calibrated?UINT32_MAX:v.fine?50:copy.mode_==Mode::LoRa?10000:UINT32_MAX;
   if(copy.mode_==Mode::Hold&&tick>=copy.last_live_)v.holdover_age_us=(tick-copy.last_live_)/20;}
  return v;
 }
 ClockView update(uint64_t tick,uint64_t domain,const Observation& ble,const Observation& lora){
  if(domain!=domain_){fault_=true;mode_=Mode::Unsynced;}
  if(fault_||!enabled_||!instance_||!domain_||!tick)return view(tick);
  if(tick_&&!advance(tick)){++rejected_;fault_=true;mode_=Mode::Unsynced;return {};}
  const bool b=ble.live(tick),l=lora.live(tick);
  // Qualification follows fresh physical BLE progress, never a painted frame.
  // A refused projection withdraws current authority, but does not erase
  // qualification while physical BLE progress is still recent. Preserve that
  // continuity across short mapping gaps; real absence revokes it after 500ms.
  // A fresh source/domain below always starts its own qualification window.
  if(ble_last_&&tick-ble_last_>10000000){ble_since_=0;ble_last_=0;}
  if(b){if(qual_scope_!=ble.scope||qual_domain_!=ble.domain){ble_since_=tick;ble_last_=0;qual_id_=0;qual_scope_=ble.scope;qual_domain_=ble.domain;}
   if(ble.id>qual_id_){if(!ble_since_)ble_since_=tick;ble_last_=tick;qual_id_=ble.id;}}
  const bool return_ready=!tick_||mode_==Mode::Ble||(b&&ble_since_&&tick>=ble_since_&&tick-ble_since_>=2000000&&ble_last_&&tick-ble_last_<10000000);
  // The receiver has already authenticated and qualified its model. On return,
  // require100ms of advancing physical observations, not an additional30s wait.
  // Calibration describes applicability of a measured offset, never admission.
  const Observation* chosen=b&&return_ready?&ble:l?&lora:nullptr;
  if(!chosen){if(tick_){mode_=Mode::Hold;phase_=0;}return view(tick);}
  calibrated_=chosen->calibrated;
  const unsigned channel=chosen==&ble?0:1;const auto next=channel?Mode::LoRa:Mode::Ble;
  // A changed context starts a new observation sequence, with fresh receiver
  // admission and explicit calibration applicability, never old permissions.
  const bool new_scope=scopes_[channel]!=chosen->scope||domains_[channel]!=chosen->domain;
  const bool handover=selected_!=channel;
  if(!new_scope&&chosen->id<ids_[channel]){++rejected_;mode_=Mode::Hold;phase_=0;return view(tick);}
  const double age=double(tick-chosen->tick)/20000000;
  const double target=tick_?double(int64_t(chosen->utc_ns)-int64_t(ns_))+double(tick-chosen->tick)*50+chosen->rate_ppb*age:0;
  if(tick_&&(!std::isfinite(target)||std::abs(target)>1e9)){++rejected_;mode_=Mode::Hold;phase_=0;return view(tick);}
  if(!new_scope&&chosen->id==ids_[channel]){
   // A withheld map cancels steering while authority is absent. On a fresh
   // valid projection, resume toward that same model without scoring another
   // physical update, learning frequency again or extending its expiry.
   if(mode_!=next)phase_=target;
   if(channel&&handover)coarse_last_=tick;
   selected_=uint8_t(channel);mode_=next;permission_=chosen->until;last_live_=tick;return view(tick);
  }
  if(!tick_){ns_=chosen->utc_ns;tick_=chosen->tick;rate_=chosen->rate_ppb;mode_=next;if(!advance(tick))return {};phase_=0;}
  else {
   // The radio model is already filtered. Follow its full phase using the
   // slower bounded coarse slew, rather than filtering its error a second time.
   phase_=target;
   if(!channel)rate_=std::clamp(rate_+.125*(chosen->rate_ppb-rate_),-50000.0,50000.0);
   else if(!handover&&!new_scope&&coarse_last_&&tick>coarse_last_){
    // Preserve the fine rate on handover, then allow fresh physical LoRa
    // updates to discipline frequency. At the 30s cadence, gain is 1/2 with
    // at most 500ppb change. Shorter intervals cannot accelerate that budget.
    const double span=std::min(1.0,double(tick-coarse_last_)/600000000);
    const double change=std::clamp(.5*span*(chosen->rate_ppb-rate_),-500*span,500*span);
    rate_=std::clamp(rate_+change,-50000.0,50000.0);
   }
  }
  if(channel)coarse_last_=tick;
  selected_=uint8_t(channel);
  ids_[channel]=chosen->id;scopes_[channel]=chosen->scope;domains_[channel]=chosen->domain;
  mode_=next;permission_=chosen->until;last_live_=tick;++updates_;return view(tick);
 }
};
}
